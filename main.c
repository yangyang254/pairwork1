#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE   4096   /* 单行文本缓冲区大小 */
#define MAX_OPS       3   /* 一道题中运算符个数上限 */
#define RAND_LIMIT   64   /* 随机尝试的最大次数 */

/* 拼字符串的小工具：把文本逐段追加到缓冲区，带边界检查，
 * 避免使用 strcat 带来的性能问题，也避免缓冲区溢出。 */
static char *g_p;                       /* 当前写入位置 */
static char *g_end;                     /* 缓冲区末尾（不含） */

static void put(const char *s)
{
    while (*s != '\0' && g_p < g_end) *g_p++ = *s++;
    *g_p = '\0';                        /* 保持字符串总是以 '\0' 结束 */
}

static void put_begin(char *buf, int size)
{
    g_p = buf;
    g_end = buf + size - 1;
    *g_p = '\0';
}

/*------------------------------ 表达式树 -----------------------------------*/
/* 用二叉树保存一道题目：叶子是数字，内部结点是一个运算符              */
typedef struct Expr
{
    int op;                 /* 0:数字叶子  1:+  2:-  3:×  4:÷ */
    long long val;          /* op==0 时保存这个自然数 */
    struct Expr *l, *r;     /* 左右子表达式 */
} Expr;

static Expr *new_expr(int op, long long val, Expr *l, Expr *r)
{
    Expr *e = (Expr *)malloc(sizeof(Expr));
    e->op = op;
    e->val = val;
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

static int rand_int(int lo, int hi)          /* 返回 [lo, hi] 内的随机整数 */
{
    return lo + rand() % (hi - lo + 1);
}

static const char *op_text(int op)           /* 运算符对应的文本 */
{
    switch (op)
    {
        case 1: return "+";
        case 2: return "-";
        case 3: return "*";
        default: return "/";
    }
}

/* 就地求值：返回该子表达式的值。若无解（出现负数或除不尽）返回 -1 */
static long long eval_expr(Expr *e)
{
    long long a, b;
    if (e->op == 0) return e->val;

    a = eval_expr(e->l);
    b = eval_expr(e->r);
    if (a < 0 || b < 0) return -1;           /* 子树已经非法 */

    switch (e->op)
    {
        case 1: return a + b;
        case 2: return (a >= b) ? a - b : -1;
        case 3: return a * b;
        default:
            if (b == 0 || a % b != 0) return -1;
            return a / b;
    }
}

/* 生成一道题：两个运算数 + 一个加、减或乘。
 *
 * 说明：
 *   1) 本版还没有括号，题面按“先乘除后加减”来读。为了不让题面和答案
 *      产生歧义（例如 55 - 38 * 3 该按哪种顺序算），本版只出一个运算符；
 *      多个运算符、括号和除法留到后面的版本再加。
 *   2) 加法、乘法的结果都不超过 range-1，减法保证 a >= b（不出现负数）。 */
static Expr *gen_expr(int ops, int range)
{
    int k;

    (void)ops;                               /* 本版固定用一个运算符 */
    for (k = 0; k < RAND_LIMIT * 2; k++)
    {
        int op = rand_int(1, 3);             /* 1:+  2:-  3:* */
        long long a = rand_int(1, range - 1);
        long long b = rand_int(1, range - 1);

        if (op == 1 && a + b > range - 1) continue;      /* 加法：和不越界 */
        if (op == 2 && a < b) continue;                  /* 减法：不出现负数 */
        if (op == 3 && a * b > range - 1) continue;      /* 乘法：积不越界 */

        return new_expr(op, 0, new_expr(0, a, NULL, NULL), new_expr(0, b, NULL, NULL));
    }
    /* 兜底：1 + 1 */
    return new_expr(1, 0, new_expr(0, 1, NULL, NULL), new_expr(0, 1, NULL, NULL));
}

/*------------------------------ 题目输出 -----------------------------------*/
/* 把表达式按中序打印出来（本版没有括号） */
static void print_expr(const Expr *e)
{
    if (e->op == 0)
    {
        char buf[32];
        sprintf(buf, "%lld", e->val);
        put(buf);
        return;
    }
    print_expr(e->l);                        /* 先左 */
    put(" ");
    put(op_text(e->op));
    put(" ");
    print_expr(e->r);                        /* 再右 */
}

/* 生成第 idx 道题，并把题目和答案分别写入两个文件 */
static int make_one(FILE *fe, FILE *fa, int range, int idx)
{
    Expr *e;
    long long ans;
    char line[MAX_LINE];
    char ansline[MAX_LINE];

    e = gen_expr(rand_int(1, MAX_OPS), range);
    ans = eval_expr(e);                          /* 生成时已保证 >= 0 */

    put_begin(line, MAX_LINE);
    put("题目");
    {
        char num[32];
        sprintf(num, "%d", idx);
        put(num);
    }
    put(": ");
    print_expr(e);
    put(" =\n");
    fputs(line, fe);

    sprintf(ansline, "答案%d: %lld\n", idx, ans);
    fputs(ansline, fa);

    free_expr(e);
    return 1;
}

/*------------------------------ 帮助信息 -----------------------------------*/
static void usage(const char *exe)
{
    printf("小学四则运算题目生成器\n");
    printf("用法：\n");
    printf("  %s -n <题目个数> -r <数值范围>\n", exe);
    printf("说明：\n");
    printf("  -n  生成题目的个数，缺省为 10\n");
    printf("  -r  题目中数值的范围（必须给出），例如 -r 10 表示 10 以内（不含 10）\n");
    printf("例如：\n");
    printf("  %s -n 10 -r 10\n", exe);
}

int main(int argc, char *argv[])
{
    int n = 10, r = 0, i;
    FILE *fe, *fa;

    srand((unsigned)time(NULL));

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            n = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            r = atoi(argv[++i]);
    }

    if (r <= 1)                              /* -r 必须给定且要能取到数值 */
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

    for (i = 1; i <= n; i++)
        make_one(fe, fa, r, i);

    fclose(fe);
    fclose(fa);
    printf("已生成 %d 道题目：Exercises.txt / Answers.txt\n", n);
    return 0;
}
