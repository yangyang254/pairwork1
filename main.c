#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE   4096     /* 单行文本缓冲区大小 */
#define MAX_OPS       3     /* 一道题中运算符个数上限 */
#define RAND_LIMIT  120     /* 随机尝试的最大次数 */
#define HASH_SIZE 262144    /* 去重哈希表桶数 */

/*------------------------------ 分数 ---------------------------------------*/
/* 用“分子/分母”表示一个数：整数写成 d==1，真分数写成 a<b */
typedef struct
{
    long long a;            /* 分子 */
    long long d;            /* 分母，恒大于 0 */
} Frac;

static long long gcd64(long long x, long long y)     /* 最大公约数 */
{
    long long t;
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y) { t = x % y; x = y; y = t; }
    return x;
}

static Frac frac_make(long long a, long long d)      /* 构造并约分到最简 */
{
    Frac f;
    long long g;
    if (d < 0) { a = -a; d = -d; }
    g = gcd64(a, d);
    if (g == 0) g = 1;
    f.a = a / g;
    f.d = d / g;
    return f;
}

static Frac frac_add(Frac x, Frac y) { return frac_make(x.a * y.d + y.a * x.d, x.d * y.d); }
static Frac frac_sub(Frac x, Frac y) { return frac_make(x.a * y.d - y.a * x.d, x.d * y.d); }
static Frac frac_mul(Frac x, Frac y) { return frac_make(x.a * y.a, x.d * y.d); }
static Frac frac_div(Frac x, Frac y) { return frac_make(x.a * y.d, x.d * y.a); }

static int frac_is_zero(Frac x) { return x.a == 0; }
static int frac_is_proper(Frac x) { return (x.a > 0 && x.a < x.d); }  /* 是否为真分数 */
static int frac_less(Frac x, Frac y) { return x.a * y.d < y.a * x.d; }

/* 把分数格式化成字符串：整数、真分数 a/b、带分数 n'a/b */
static void frac_text(Frac v, char *buf, int size)
{
    if (v.d == 1)
        snprintf(buf, size, "%lld", v.a);
    else if (v.a < v.d)
        snprintf(buf, size, "%lld/%lld", v.a, v.d);
    else
        snprintf(buf, size, "%lld'%lld/%lld", v.a / v.d, v.a % v.d, v.d);
}

/*------------------------------ 拼串工具 -----------------------------------*/
/* 把文本逐段追加到缓冲区，带边界检查：避免 strcat 的性能开销，也避免溢出 */
static char *g_p;                       /* 当前写入位置 */
static char *g_end;                     /* 缓冲区末尾（不含） */

static void put(const char *s)
{
    while (*s != '\0' && g_p < g_end) *g_p++ = *s++;
    *g_p = '\0';                        /* 保证缓冲区始终以 '\0' 结束 */
}

static void put_frac(Frac v)
{
    char buf[64];
    frac_text(v, buf, sizeof(buf));
    put(buf);
}

/* 往缓冲区里写一个字符（带边界检查） */
static void put_ch(char c)
{
    if (g_p < g_end) { *g_p++ = c; *g_p = '\0'; }
}

/* 把整数按十进制直接写进缓冲区，省掉一次 sprintf —— 生成一万道题时
 * 规范形式要反复拼叶子，这个小函数能省下不少时间 */
static void put_ll(long long x)
{
    char tmp[24];
    int n = 0;

    if (x < 0) { put_ch('-'); x = -x; }
    if (x == 0) { put_ch('0'); return; }
    while (x > 0) { tmp[n++] = (char)('0' + (int)(x % 10)); x /= 10; }
    while (n > 0) put_ch(tmp[--n]);
}

/* 不经过临时缓冲区，直接把分数写进目标缓冲区 */
static void put_frac_raw(Frac v)
{
    if (v.d == 1) put_ll(v.a);
    else if (v.a < v.d) { put_ll(v.a); put_ch('/'); put_ll(v.d); }
    else { put_ll(v.a / v.d); put_ch('\''); put_ll(v.a % v.d); put_ch('/'); put_ll(v.d); }
}

static void put_begin(char *buf, int size)
{
    g_p = buf;
    g_end = buf + size - 1;
    *g_p = '\0';
}

/*------------------------------ 表达式树 -----------------------------------*/
typedef struct Expr
{
    int op;                 /* 0:叶子  1:+  2:-  3:*  4:/ */
    Frac val;               /* op==0 时保存这个数 */
    struct Expr *l, *r;     /* 左右子表达式 */
} Expr;

static Expr *new_leaf(Frac v)
{
    Expr *e = (Expr *)malloc(sizeof(Expr));
    e->op = 0;
    e->val = v;
    e->l = e->r = NULL;
    return e;
}

static Expr *new_node(int op, Expr *l, Expr *r)
{
    Expr *e = (Expr *)malloc(sizeof(Expr));
    e->op = op;
    e->val = frac_make(0, 1);
    e->l = l;
    e->r = r;
    return e;
}

static void free_expr(Expr *e)
{
    if (e == NULL) return;
    free_expr(e->l);
    free_expr(e->r);
    free(e);
}

static int rand_int(int lo, int hi)                  /* [lo, hi] 内的随机整数 */
{
    return lo + rand() % (hi - lo + 1);
}

static const char *op_text(int op)
{
    switch (op)
    {
        case 1: return "+";
        case 2: return "-";
        case 3: return "*";
        default: return "/";
    }
}

static int op_prio(int op)                           /* 运算符优先级 */
{
    return (op == 1 || op == 2) ? 1 : 2;
}

/*------------------------------ 求值与校验 ---------------------------------*/
/* eval_expr 只负责“算”，不管题目是否合题意 */
static void eval_expr(const Expr *e, Frac *out)
{
    Frac a, b;

    if (e->op == 0) { *out = e->val; return; }
    eval_expr(e->l, &a);
    eval_expr(e->r, &b);
    switch (e->op)
    {
        case 1:  *out = frac_add(a, b); break;
        case 2:  *out = frac_sub(a, b); break;
        case 3:  *out = frac_mul(a, b); break;
        default: *out = frac_div(a, b); break;
    }
}

/* validate_expr 按题意检查：不出现负数、除数不为 0、除法结果是真分数 */
static int validate_expr(const Expr *e, Frac *out)
{
    Frac a, b, r;

    if (e->op == 0) { *out = e->val; return 1; }
    if (!validate_expr(e->l, &a)) return 0;
    if (!validate_expr(e->r, &b)) return 0;

    switch (e->op)
    {
        case 1: r = frac_add(a, b); break;
        case 2:
            if (frac_less(a, b)) return 0;           /* e1 >= e2，不出现负数 */
            r = frac_sub(a, b);
            break;
        case 3: r = frac_mul(a, b); break;
        default:
            if (frac_is_zero(b)) return 0;           /* 除数不能为 0 */
            r = frac_div(a, b);
            if (!frac_is_proper(r)) return 0;        /* 除法结果必须是真分数 */
            break;
    }
    *out = r;
    return 1;
}

/* 随机取一个运算数：自然数（1 .. range-1）或真分数，分子分母都小于 range */
static Frac gen_atom(int range)
{
    if (range <= 2)
        return frac_make(1, 1);                      /* 范围太小时只有 1 */

    if (rand() % 2 == 0)
        return frac_make(rand_int(1, range - 1), 1);

    {
        int d = rand_int(2, range - 1);              /* 真分数分母也小于 range */
        int a = rand_int(1, d - 1);
        return frac_make(a, d);
    }
}

/* 生成一棵合法表达式树；ops 为剩余可用的运算符个数 */
static Expr *gen_expr(int ops, int range)
{
    int i, op, left_ops;

    if (ops == 0) return new_leaf(gen_atom(range));

    for (i = 0; i < RAND_LIMIT; i++)
    {
        Expr *e;
        Frac v;

        op = rand_int(1, 4);
        left_ops = rand_int(0, ops - 1);

        e = new_node(op, gen_expr(left_ops, range), gen_expr(ops - 1 - left_ops, range));
        if (validate_expr(e, &v)) return e;          /* 合法就采用 */
        free_expr(e);                                /* 否则换一个运算符重试 */
    }
    return new_leaf(gen_atom(range));                /* 兜底，保证能返回 */
}

/*------------------------------ 题面输出（带括号） -------------------------*/
/* 判断子表达式作为父亲的操作数时是否需要括号 */
static int need_paren(const Expr *child, const Expr *parent, int is_right)
{
    if (child->op == 0)
    {
        /* 叶子一般不用括号；但如果这个叶子是分数，而父亲是 × 或 ÷，
         * 就必须加括号，否则 "80 * 3/4 / 27" 会产生歧义：
         * 分不清 3/4 是一个分数，还是 "80 * 3 ÷ 4"。 */
        if (child->val.d != 1 && (parent->op == 3 || parent->op == 4)) return 1;
        return 0;
    }
    if (op_prio(child->op) < op_prio(parent->op)) return 1;    /* 优先级低要加 */
    if (op_prio(child->op) > op_prio(parent->op)) return 0;    /* 优先级高不加 */

    /* 同优先级时，- 和 / 的右操作数必须加括号：8 / (4 / 2)、7 - (3 - 1) */
    return (is_right && (parent->op == 2 || parent->op == 4));
}

static void print_expr(const Expr *e)
{
    int lp, rp;

    if (e->op == 0) { put_frac(e->val); return; }

    lp = need_paren(e->l, e, 0);
    rp = need_paren(e->r, e, 1);

    if (lp) put("( ");
    print_expr(e->l);
    if (lp) put(" )");

    put(" ");
    put(op_text(e->op));
    put(" ");

    if (rp) put("( ");
    print_expr(e->r);
    if (rp) put(" )");
}

/*------------------------------ 表达式解析器 -------------------------------*/
/* 递归下降：parse_atom / parse_term / parse_expr 逐层处理括号、×÷、+-。
 * 同一套文法同时提供两种解析结果：
 *   parse_line    —— 直接算出数值（批改答案时用）
 *   parse_to_tree —— 还原成表达式树（比较题目结构时用）
 * 文法：e = n | a/b | n'a/b | (e) | e+e | e-e | e*e | e/e */
static const char *g_s;                              /* 待解析的字符串 */

static void skip_space(void) { while (*g_s == ' ') g_s++; }

static int parse_expr_value(Frac *out);              /* 前置声明 */
static Expr *parse_expr_tree(void);
static Expr *parse_to_tree(const char *s);

static int parse_atom_value(Frac *out)               /* 只解析一个“数”或括号 */
{
    long long whole, num, den;

    skip_space();
    if (*g_s == '(')
    {
        Frac v;
        g_s++;
        if (!parse_expr_value(&v)) return 0;
        skip_space();
        if (*g_s != ')') return 0;
        g_s++;
        *out = v;
        return 1;
    }

    if (*g_s < '0' || *g_s > '9') return 0;
    whole = 0;
    while (*g_s >= '0' && *g_s <= '9') whole = whole * 10 + (*g_s++ - '0');

    if (*g_s == '\'')                                /* 带分数 n'a/b */
    {
        g_s++;
        if (*g_s < '0' || *g_s > '9') return 0;
        num = 0;
        while (*g_s >= '0' && *g_s <= '9') num = num * 10 + (*g_s++ - '0');
        if (*g_s != '/') return 0;
        g_s++;
        if (*g_s < '0' || *g_s > '9') return 0;
        den = 0;
        while (*g_s >= '0' && *g_s <= '9') den = den * 10 + (*g_s++ - '0');
        if (den == 0) return 0;
        *out = frac_make(whole * den + num, den);
        return 1;
    }

    if (*g_s == '/')                                 /* 真分数 a/b */
    {
        g_s++;
        if (*g_s < '0' || *g_s > '9') return 0;
        den = 0;
        while (*g_s >= '0' && *g_s <= '9') den = den * 10 + (*g_s++ - '0');
        if (den == 0) return 0;
        *out = frac_make(whole, den);
        return 1;
    }

    *out = frac_make(whole, 1);                      /* 自然数 */
    return 1;
}

static int parse_term_value(Frac *out)               /* 只处理 × 和 ÷ */
{
    Frac left, right;

    if (!parse_atom_value(&left)) return 0;
    for (;;)
    {
        char op;
        skip_space();
        if (*g_s != '*' && *g_s != '/') break;
        op = *g_s++;
        if (!parse_atom_value(&right)) return 0;
        left = (op == '*') ? frac_mul(left, right) : frac_div(left, right);
    }
    *out = left;
    return 1;
}

static int parse_expr_value(Frac *out)               /* 处理 + 和 - */
{
    Frac left, right;

    if (!parse_term_value(&left)) return 0;
    for (;;)
    {
        char op;
        skip_space();
        if (*g_s != '+' && *g_s != '-') break;
        op = *g_s++;
        if (!parse_term_value(&right)) return 0;
        left = (op == '+') ? frac_add(left, right) : frac_sub(left, right);
    }
    *out = left;
    return 1;
}

static Expr *parse_atom_tree(void)
{
    Expr *e;
    Frac v;

    skip_space();
    if (*g_s == '(')
    {
        g_s++;
        e = parse_expr_tree();
        if (e == NULL) return NULL;
        skip_space();
        if (*g_s != ')') { free_expr(e); return NULL; }
        g_s++;
        return e;
    }

    if (!parse_atom_value(&v)) return NULL;
    return new_leaf(v);
}

static Expr *parse_term_tree(void)
{
    Expr *left = parse_atom_tree();

    if (left == NULL) return NULL;
    for (;;)
    {
        char op;
        skip_space();
        if (*g_s != '*' && *g_s != '/') break;
        op = *g_s++;
        {
            Expr *right = parse_atom_tree();
            if (right == NULL) { free_expr(left); return NULL; }
            left = new_node((op == '*') ? 3 : 4, left, right);
        }
    }
    return left;
}

static Expr *parse_expr_tree(void)
{
    Expr *left = parse_term_tree();

    if (left == NULL) return NULL;
    for (;;)
    {
        char op;
        skip_space();
        if (*g_s != '+' && *g_s != '-') break;
        op = *g_s++;
        {
            Expr *right = parse_term_tree();
            if (right == NULL) { free_expr(left); return NULL; }
            left = new_node((op == '+') ? 1 : 2, left, right);
        }
    }
    return left;
}

/* 解析整段表达式并求值；strict=1 时顺便按题意校验 */
static int parse_line(const char *s, Frac *out, int strict)
{
    Expr *e = parse_to_tree(s);

    if (e == NULL) return 0;
    if (strict)
    {
        int good = validate_expr(e, out);
        free_expr(e);
        return good;
    }
    eval_expr(e, out);
    free_expr(e);
    return 1;
}

/* 把整段表达式解析成树，失败返回 NULL */
static Expr *parse_to_tree(const char *s)
{
    Expr *e;

    g_s = s;
    e = parse_expr_tree();
    if (e == NULL) return NULL;
    skip_space();
    if (*g_s != '\0') { free_expr(e); return NULL; }
    return e;
}

/*------------------------------ 规范形式与去重 -----------------------------*/
/* 规范字符串：+ 和 * 的操作数展开后排序（交换律、结合律），
 * - 和 / 保持左右顺序，括号被完整保留，
 * 所以 (3+4)*5 与 3+4*5 不会被判为重复。 */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void canon_to(const Expr *e, char *buf, int size)
{
    char sub[8][2048];
    const char *p[8];
    int n = 0, i;

    if (e->op == 0)                                  /* 叶子：直接用数值 */
    {
        g_p = buf;  g_end = buf + size - 1;  *g_p = '\0';
        put_frac_raw(e->val);                        /* 直接写数，不经过 sprintf */
        return;
    }

    if (e->op == 1 || e->op == 3)                    /* + 和 * ：展开后排序 */
    {
        const Expr *stack[8];
        int top = 0;
        stack[top++] = e;
        while (top > 0)
        {
            const Expr *cur = stack[--top];
            if (cur->op == e->op && n + top + 2 <= 8)
            {
                stack[top++] = cur->r;
                stack[top++] = cur->l;
            }
            else
            {
                canon_to(cur, sub[n], sizeof(sub[n]));
                n++;
            }
        }
        for (i = 0; i < n; i++) p[i] = sub[i];
        qsort(p, n, sizeof(p[0]), cmp_str);
    }
    else                                             /* - 和 / ：保持左右顺序 */
    {
        canon_to(e->l, sub[0], sizeof(sub[0]));
        canon_to(e->r, sub[1], sizeof(sub[1]));
        p[0] = sub[0];
        p[1] = sub[1];
        n = 2;
    }

    g_p = buf;  g_end = buf + size - 1;  *g_p = '\0';
    put("(");
    for (i = 0; i < n; i++)
    {
        if (i) { put(" "); put(op_text(e->op)); put(" "); }
        put(p[i]);
    }
    put(")");
}

/* 判断表达式树与它的题面文本是否完全一致（用来发现打印歧义） */
static int canon_equal(const Expr *e, const char *text)
{
    Expr *back = parse_to_tree(text);
    char k1[2048], k2[2048];
    int same;

    if (back == NULL) return 0;
    canon_to(e, k1, sizeof(k1));
    canon_to(back, k2, sizeof(k2));
    same = (strcmp(k1, k2) == 0);
    free_expr(back);
    return same;
}

/* 哈希表：保存已经出现过的题目的规范形式 */
typedef struct HashNode
{
    char *key;
    struct HashNode *next;
} HashNode;

static HashNode *g_table[HASH_SIZE];

static unsigned int hash_str(const char *s)
{
    unsigned int h = 2166136261u;                    /* FNV-1a 哈希 */
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h % HASH_SIZE;
}

/* 返回 1 表示新题目（已插入），0 表示重复（重复的不会再插入） */
static int hash_add_unique(const char *key)
{
    unsigned int h = hash_str(key);
    HashNode *p;

    for (p = g_table[h]; p != NULL; p = p->next)
        if (strcmp(p->key, key) == 0) return 0;

    p = (HashNode *)malloc(sizeof(HashNode));
    p->key = (char *)malloc(strlen(key) + 1);
    strcpy(p->key, key);
    p->next = g_table[h];
    g_table[h] = p;
    return 1;
}

/*------------------------------ 生成模式 -----------------------------------*/
/* 生成一道新题目：不重复，且题目文本解析出来的结构、答案都与原树一致 */
static Expr *gen_unique(int range, Frac *ans, int *dup_try)
{
    int guard = 0;

    for (;;)
    {
        Expr *e = gen_expr(rand_int(1, MAX_OPS), range);
        char key[2048];
        char text[MAX_LINE];
        Frac by_parse;

        if (!validate_expr(e, ans)) { free_expr(e); continue; }

        canon_to(e, key, sizeof(key));
        if (!hash_add_unique(key))                   /* 与已有题目重复 */
        {
            (*dup_try)++;
            free_expr(e);
            if (guard++ < 1000) continue;            /* 再抽一道 */
        }

        put_begin(text, MAX_LINE);                   /* 自检一：题面能算出同样的答案 */
        print_expr(e);
        if (parse_line(text, &by_parse, 0) &&
            by_parse.a == ans->a && by_parse.d == ans->d &&
            canon_equal(e, text))                    /* 自检二：题面无歧义 */
            return e;

        free_expr(e);                                /* 自检失败，弃用重来 */
        if (guard++ > 5000) return NULL;
    }
}

static void make_one(FILE *fe, FILE *fa, int range, int idx, int *dup_total)
{
    Expr *e;
    Frac ans;
    char line[MAX_LINE];
    char ansline[MAX_LINE];
    char num[64];
    int dup_try = 0;

    e = gen_unique(range, &ans, &dup_try);
    *dup_total += dup_try;
    if (e == NULL) return;

    put_begin(line, MAX_LINE);
    put("题目");
    snprintf(num, sizeof(num), "%d", idx);
    put(num);    put(": ");
    print_expr(e);
    put(" =\n");
    fputs(line, fe);

    put_begin(ansline, MAX_LINE);
    put("答案");
    snprintf(num, sizeof(num), "%d", idx);
    put(num);    put(": ");
    put_frac(ans);
    put("\n");
    fputs(ansline, fa);

    free_expr(e);
}

/* 生成模式主流程 */
static int run_generate(int n, int r)
{
    FILE *fe, *fa;
    int i, dup_total = 0;
    clock_t t0, t1;

    fe = fopen("Exercises.txt", "w");
    fa = fopen("Answers.txt", "w");
    if (fe == NULL || fa == NULL)
    {
        printf("错误：无法在当前目录创建 Exercises.txt / Answers.txt\n");
        return 1;
    }

    t0 = clock();
    for (i = 1; i <= n; i++)
        make_one(fe, fa, r, i, &dup_total);
    t1 = clock();

    fclose(fe);
    fclose(fa);
    {
        double ms = (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC;
        printf("已生成 %d 道题目：Exercises.txt / Answers.txt\n", n);
        printf("生成过程中丢弃的重复题目：%d 道\n", dup_total);
        printf("总耗时：%.1f 毫秒（约 %.0f 道/秒）\n",
               ms, (ms > 0) ? n * 1000.0 / ms : 0.0);
    }
    return 0;
}

/*------------------------------ 批改模式 -----------------------------------*/
/* 读取 "题目12: 3 + 4 =" 这样的行，取出 ':' 后面的表达式（去掉尾部的 =） */
static int cut_exercise(const char *line, char *out, int size)
{
    const char *p = strchr(line, ':');
    int len;

    if (p == NULL) return 0;
    p++;
    while (*p == ' ') p++;
    strncpy(out, p, size - 1);
    out[size - 1] = '\0';

    len = (int)strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' ||
                       out[len - 1] == ' '  || out[len - 1] == '='))
        out[--len] = '\0';
    return 1;
}

/* 读取 "答案12: 7" 这样的行，取出 ':' 后面的答案文本 */
static int cut_answer(const char *line, char *out, int size)
{
    const char *p = strchr(line, ':');
    int len;

    if (p == NULL) return 0;
    p++;
    while (*p == ' ') p++;
    strncpy(out, p, size - 1);
    out[size - 1] = '\0';

    len = (int)strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' '))
        out[--len] = '\0';
    return 1;
}

/* 把批改结果按题目编号列表打印到文件，例如 (1, 3, 5) */
static void write_index_list(FILE *f, const int *idx, int cnt)
{
    int i;
    fprintf(f, "(");
    for (i = 0; i < cnt; i++)
        fprintf(f, "%s%d", (i > 0) ? ", " : "", idx[i]);
    fprintf(f, ")");
}

/* 批改模式主流程：-e 题目文件，-a 答案文件 */
static int run_grade(const char *exe_file, const char *ans_file)
{
    FILE *fe = fopen(exe_file, "r");
    FILE *fa = fopen(ans_file, "r");
    FILE *fg;
    char le[MAX_LINE], la[MAX_LINE];
    int *right = NULL, *wrong = NULL;
    int n_right = 0, n_wrong = 0, count = 0;
    int cap = 1024;
    clock_t t0, t1;

    if (fe == NULL || fa == NULL)
    {
        printf("错误：无法打开题目文件 %s 或答案文件 %s\n", exe_file, ans_file);
        return 1;
    }

    right = (int *)malloc(sizeof(int) * cap);
    wrong = (int *)malloc(sizeof(int) * cap);

    t0 = clock();
    while (fgets(le, MAX_LINE, fe) != NULL)
    {
        char expr[MAX_LINE], user_ans[MAX_LINE];
        Frac std_ans, my_ans;
        int no;

        if (fgets(la, MAX_LINE, fa) == NULL) break;      /* 答案文件行数不足 */
        count++;
        if (count > cap)                                 /* 题量很大时自动扩容 */
        {
            cap *= 2;
            right = (int *)realloc(right, sizeof(int) * cap);
            wrong = (int *)realloc(wrong, sizeof(int) * cap);
            if (right == NULL || wrong == NULL)
            {
                printf("错误：内存不足，无法继续批改。\n");
                break;
            }
        }

        if (!cut_exercise(le, expr, sizeof(expr)) ||
            !cut_answer(la, user_ans, sizeof(user_ans)))
        {
            wrong[n_wrong++] = count;
            continue;
        }

        /* 题目编号取 ':' 前面的数字，保证 Grade.txt 里的编号与题目一致 */
        no = count;
        {
            const char *p = le;
            while (*p && (*p < '0' || *p > '9')) p++;
            if (*p) no = atoi(p);
        }

        if (!parse_line(expr, &std_ans, 1))              /* 题目本身要符合题意 */
        {
            wrong[n_wrong++] = no;
            continue;
        }

        /* 学生答案支持 3/5、2'3/8 这样的分数写法，只比较数值 */
        if (parse_line(user_ans, &my_ans, 0) &&
            my_ans.a == std_ans.a && my_ans.d == std_ans.d)
            right[n_right++] = no;
        else
            wrong[n_wrong++] = no;
    }
    t1 = clock();

    fclose(fe);
    fclose(fa);

    fg = fopen("Grade.txt", "w");
    if (fg == NULL)
    {
        printf("错误：无法创建 Grade.txt\n");
        free(right); free(wrong);
        return 1;
    }
    fprintf(fg, "Correct: %d ", n_right);
    write_index_list(fg, right, n_right);
    fprintf(fg, "\nWrong: %d ", n_wrong);
    write_index_list(fg, wrong, n_wrong);
    fprintf(fg, "\n");
    fclose(fg);

    printf("共批改 %d 道题：正确 %d 道，错误 %d 道，结果已写入 Grade.txt\n",
           count, n_right, n_wrong);
    printf("批改耗时：%.3f 秒\n", (double)(t1 - t0) / CLOCKS_PER_SEC);

    free(right);
    free(wrong);
    return 0;
}

/*------------------------------ 帮助信息 -----------------------------------*/
static void usage(const char *exe)
{
    printf("小学四则运算题目生成器（结对项目）\n\n");
    printf("用法一（生成题目）：\n");
    printf("  %s -n <题目个数> -r <数值范围>\n", exe);
    printf("用法二（批改答案）：\n");
    printf("  %s -e <题目文件> -a <答案文件>\n\n", exe);
    printf("参数说明：\n");
    printf("  -n  生成题目的个数，缺省为 10\n");
    printf("  -r  题目中数值（自然数、真分数及其分母）的范围，必须给出\n");
    printf("      例如 -r 10 表示 10 以内（不包括 10）\n");
    printf("  -e  题目文件（Exercises.txt）\n");
    printf("  -a  答案文件（Answers.txt），批改结果写入 Grade.txt\n\n");
    printf("例如：\n");
    printf("  %s -n 10 -r 10\n", exe);
    printf("  %s -e Exercises.txt -a Answers.txt\n", exe);
}

#ifndef MYAPP_NO_MAIN          /* 供 test.c 复用本文件的函数时屏蔽 */
int main(int argc, char *argv[])
{
    int n = 10, r = 0, i;
    const char *exe_file = NULL, *ans_file = NULL;

    srand((unsigned)time(NULL));

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            n = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            r = atoi(argv[++i]);
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
            exe_file = argv[++i];
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            ans_file = argv[++i];
        else
        {
            printf("错误：无法识别的参数 %s\n\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (exe_file != NULL || ans_file != NULL)            /* 批改模式 */
    {
        if (exe_file == NULL || ans_file == NULL)
        {
            printf("错误：批改时必须同时给出 -e 和 -a 两个参数。\n\n");
            usage(argv[0]);
            return 1;
        }
        return run_grade(exe_file, ans_file);
    }

    if (r <= 1)                                          /* 生成模式：-r 必须给出 */
    {
        printf("错误：必须用 -r 指定数值范围，且范围要大于 1。\n\n");
        usage(argv[0]);
        return 1;
    }
    if (n < 0) n = 0;

    return run_generate(n, r);
}
#endif /* MYAPP_NO_MAIN */
