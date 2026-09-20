#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE   4096   /* 单行文本缓冲区大小 */
#define MAX_OPS       3   /* 一道题中运算符个数上限 */
#define RAND_LIMIT  120   /* 随机尝试的最大次数 */
#define HASH_SIZE 262144  /* 去重哈希表桶数 */

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

static Frac frac_make(long long a, long long d)      /* 构造并约分 */
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

/* 把分数格式化成字符串：整数、真分数 a/b、带分数 n'a/b */
static void frac_text(Frac v, char *buf, int size)
{
    if (v.d == 1)
        sprintf(buf, "%lld", v.a);
    else if (v.a < v.d)
        sprintf(buf, "%lld/%lld", v.a, v.d);
    else
        sprintf(buf, "%lld'%lld/%lld", v.a / v.d, v.a % v.d, v.d);
    (void)size;
}

/*------------------------------ 拼串工具 -----------------------------------*/
/* 把文本逐段追加到缓冲区，带边界检查：既避免 strcat 的性能开销，也避免溢出 */
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

/* 按题意求值：返回 0 表示不合法（负数、除零、结果不是真分数） */
static int eval_expr(const Expr *e, Frac *out)
{
    Frac a, b, r;

    if (e->op == 0) { *out = e->val; return 1; }
    if (!eval_expr(e->l, &a)) return 0;
    if (!eval_expr(e->r, &b)) return 0;

    switch (e->op)
    {
        case 1: r = frac_add(a, b); break;
        case 2:
            if (a.a * b.d < b.a * a.d) return 0;     /* e1 >= e2，不出现负数 */
            r = frac_sub(a, b);
            break;
        case 3: r = frac_mul(a, b); break;
        default:
            if (frac_is_zero(b)) return 0;           /* 除数不能为 0 */
            if (a.a * b.d >= b.a * a.d) return 0;    /* 结果必须是真分数 */
            r = frac_div(a, b);
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
        int d = rand_int(2, range - 1);
        int a = rand_int(1, d - 1);
        return frac_make(a, d);
    }
}

/* 生成一棵合法表达式树；ops 为剩余可用的运算符个数，depth 用于控制括号深度 */
static Expr *gen_expr(int ops, int range, int depth)
{
    int i, op, left_ops;

    if (ops == 0) return new_leaf(gen_atom(range));

    for (i = 0; i < RAND_LIMIT; i++)
    {
        Expr *e;
        Frac v;

        op = rand_int(1, 4);
        left_ops = rand_int(0, ops - 1);

        e = new_node(op, gen_expr(left_ops, range, depth + 1),
                         gen_expr(ops - 1 - left_ops, range, depth + 1));
        if (eval_expr(e, &v)) return e;              /* 合法就采用 */
        free_expr(e);                                /* 否则换一个运算符重试 */
    }
    return new_leaf(gen_atom(range));                /* 兜底，保证能返回 */
}

/*------------------------------ 题目输出 -----------------------------------*/
/* need_paren：这个子表达式作为父亲的操作数时是否需要写括号 */
static int need_paren(const Expr *child, const Expr *parent, int is_right)
{
    if (child->op == 0)
    {
        /* 叶子一般不用括号；但如果这个叶子是分数，而父亲是 × 或 ÷，
         * 就必须加括号，否则 "80 * 3/4 / 27" 会有歧义：
         * 分不清 3/4 是一个分数还是 "80*3 ÷ 4"。 */
        if (child->val.d != 1 && (parent->op == 3 || parent->op == 4)) return 1;
        return 0;
    }
    if (op_prio(child->op) < op_prio(parent->op)) return 1;   /* 优先级低要加 */
    if (op_prio(child->op) > op_prio(parent->op)) return 0;   /* 优先级高不加 */

    /* 同优先级：- 和 / 的右操作数必须加括号，例如 8 / (4 / 2)、7 - (3 - 1) */
    if (is_right && (parent->op == 2 || parent->op == 4)) return 1;
    return 0;
}

static void print_expr(const Expr *e)                /* 输出题面，带必要的括号 */
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
/* 递归下降解析题目字符串，顺便按题意校验（负数、除零、除法结果必须为真分数） */
static const char *g_s;                              /* 待解析的字符串 */

static void skip_space(void) { while (*g_s == ' ') g_s++; }

static int parse_expr(Frac *out);

static int parse_atom(Frac *out)                     /* 解析 数字 / 真分数 / 带分数 / ( e ) */
{
    long long whole, num, den;

    skip_space();
    if (*g_s == '(')
    {
        g_s++;
        if (!parse_expr(out)) return 0;
        skip_space();
        if (*g_s != ')') return 0;
        g_s++;
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

static int parse_term(Frac *out)                     /* 只处理 × 和 ÷ */
{
    Frac left, right, r;
    if (!parse_atom(&left)) return 0;

    for (;;)
    {
        skip_space();
        if (*g_s != '*' && *g_s != '/') break;
        {
            char op = *g_s++;
            if (!parse_atom(&right)) return 0;
            if (op == '*') r = frac_mul(left, right);
            else
            {
                if (frac_is_zero(right)) return 0;   /* 除数不能为 0 */
                if (left.a * right.d >= right.a * left.d) return 0;  /* 必须是真分数 */
                r = frac_div(left, right);
            }
            left = r;
        }
    }
    *out = left;
    return 1;
}

static int parse_expr(Frac *out)                     /* 处理 + 和 -（最低优先级） */
{
    Frac left, right, r;
    if (!parse_term(&left)) return 0;

    for (;;)
    {
        skip_space();
        if (*g_s != '+' && *g_s != '-') break;
        {
            char op = *g_s++;
            if (!parse_term(&right)) return 0;
            if (op == '+') r = frac_add(left, right);
            else
            {
                if (left.a * right.d < right.a * left.d) return 0;   /* 不出现负数 */
                r = frac_sub(left, right);
            }
            left = r;
        }
    }
    *out = left;
    return 1;
}

/* 解析整道题面（不含尾部的 " ="）：成功返回 1 */
static int parse_line(const char *s, Frac *out)
{
    g_s = s;
    if (!parse_expr(out)) return 0;
    skip_space();
    return (*g_s == '\0');                           /* 必须整行正好解析完 */
}

/* 只按语法把题面还原成表达式树（不校验题意），失败返回 NULL。
 * 用来比较“题面读出来的结构”和“生成时的结构”是否一致。 */
static Expr *parse_expr_tree(void);

static Expr *parse_atom_tree(void)
{
    Expr *e;
    long long whole, num, den;
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

    if (*g_s < '0' || *g_s > '9') return NULL;
    whole = 0;
    while (*g_s >= '0' && *g_s <= '9') whole = whole * 10 + (*g_s++ - '0');
    if (*g_s == '\'')
    {
        g_s++;
        if (*g_s < '0' || *g_s > '9') return NULL;
        num = 0;
        while (*g_s >= '0' && *g_s <= '9') num = num * 10 + (*g_s++ - '0');
        if (*g_s != '/') return NULL;
        g_s++;
        if (*g_s < '0' || *g_s > '9') return NULL;
        den = 0;
        while (*g_s >= '0' && *g_s <= '9') den = den * 10 + (*g_s++ - '0');
        if (den == 0) return NULL;
        v = frac_make(whole * den + num, den);
    }
    else if (*g_s == '/')
    {
        g_s++;
        if (*g_s < '0' || *g_s > '9') return NULL;
        den = 0;
        while (*g_s >= '0' && *g_s <= '9') den = den * 10 + (*g_s++ - '0');
        if (den == 0) return NULL;
        v = frac_make(whole, den);
    }
    else
        v = frac_make(whole, 1);
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
 * 括号被完整保留，所以 (3+4)*5 与 3+4*5 不会被误判为重复。 */
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
        frac_text(e->val, buf, 64);
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

/* 哈希表：保存已经出现过的题目的规范形式 */typedef struct HashNode
{
    char *key;
    struct HashNode *next;
} HashNode;

static HashNode *g_table[HASH_SIZE];

static unsigned int hash_str(const char *s)
{
    unsigned int h = 2166136261u;                    /* FNV-1a */
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

/* 生成一道新题目：保证不重复，且“题面解析出来的答案”等于“树求值答案” */
static Expr *gen_unique(int range, Frac *ans, int *dup_try)
{
    int guard = 0;
    *dup_try = 0;

    for (;;)
    {
        Expr *e = gen_expr(rand_int(1, MAX_OPS), range, 0);
        char key[2048];
        char text[MAX_LINE];
        Frac by_parse;

        if (!eval_expr(e, ans)) { free_expr(e); continue; }

        canon_to(e, key, sizeof(key));
        if (!hash_add_unique(key))                       /* 与已有题目重复 */
        {
            (*dup_try)++;
            if (guard++ < 1000)                          /* 再抽一道 */
            {
                free_expr(e);
                continue;
            }
            /* 抽样太多次都重复时接受它，保证程序一定能结束 */
        }

        /* 自检一：把题面重新解析一遍，答案必须一致；
         * 自检二：题面解析出来的结构必须和原来一样（否则题面有歧义） */
        put_begin(text, MAX_LINE);
        print_expr(e);
        if (parse_line(text, &by_parse) &&
            by_parse.a == ans->a && by_parse.d == ans->d &&
            canon_equal(e, text))
            return e;

        free_expr(e);                                    /* 自检失败，重来 */
        if (guard++ > 2000) return NULL;
    }
}

/* 写出一道题目及其答案 */
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
    sprintf(num, "%d", idx);
    put(num);    put(": ");
    print_expr(e);
    put(" =\n");
    fputs(line, fe);

    put_begin(ansline, MAX_LINE);
    put("答案");
    sprintf(num, "%d", idx);
    put(num);    put(": ");
    put_frac(ans);
    put("\n");
    fputs(ansline, fa);

    free_expr(e);
}

/*------------------------------ 主程序 -------------------------------------*/
static void usage(const char *exe)
{
    printf("小学四则运算题目生成器\n");
    printf("用法：\n");
    printf("  %s -n <题目个数> -r <数值范围>\n", exe);
    printf("说明：\n");
    printf("  -n  生成题目的个数，缺省为 10\n");
    printf("  -r  题目中数值（自然数、真分数及其分母）的范围，必须给出\n");
    printf("例如：\n");
    printf("  %s -n 10 -r 10\n", exe);
}

int main(int argc, char *argv[])
{
    int n = 10, r = 0, i;
    int dup_total = 0;
    FILE *fe, *fa;
    clock_t t0, t1;

    srand((unsigned)time(NULL));

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            n = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            r = atoi(argv[++i]);
    }

    if (r <= 1)
    {
        printf("错误：必须用 -r 指定数值范围，且范围要大于 1。\n\n");
        usage(argv[0]);
        return 1;
    }
    if (n < 0) n = 0;

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
    printf("已生成 %d 道题目：Exercises.txt / Answers.txt\n", i - 1);
    printf("去重丢弃的重复题目：%d 道；耗时 %.3f 秒\n",
           dup_total, (double)(t1 - t0) / CLOCKS_PER_SEC);
    return 0;
}
