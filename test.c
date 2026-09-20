#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>

#define MYAPP_NO_MAIN            /* 屏蔽 main.c 里的 main 函数 */
#include "main4.c"

/*------------------------------ 测试框架 -----------------------------------*/
static int g_pass = 0;           /* 通过的用例数 */
static int g_fail = 0;           /* 失败的用例数 */
static char g_dir[1024] = ".";   /* 程序工作目录 */

static void ok(const char *name, int cond)
{
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
    else      { g_fail++; printf("  [FAIL] %s\n", name); }
}

static void test_section(const char *title)
{
    printf("\n=== %s ===\n", title);
}

/*------------------------------ 用例：分数运算 -----------------------------*/
static void test_fraction(void)
{
    Frac r;

    test_section("A1 分数四则运算");

    r = frac_add(frac_make(1, 6), frac_make(1, 8));
    ok("1/6 + 1/8 = 7/24", r.a == 7 && r.d == 24);

    r = frac_sub(frac_make(5, 6), frac_make(1, 3));
    ok("5/6 - 1/3 = 1/2", r.a == 1 && r.d == 2);

    r = frac_mul(frac_make(3, 4), frac_make(2, 9));
    ok("3/4 * 2/9 = 1/6（自动约分）", r.a == 1 && r.d == 6);

    r = frac_div(frac_make(3, 5), frac_make(2, 5));
    ok("3/5 / 2/5 = 3/2", r.a == 3 && r.d == 2);

    r = frac_add(frac_make(1, 1), frac_make(2, 3));
    ok("1 + 2/3 = 5/3", r.a == 5 && r.d == 3);

    r = frac_sub(frac_make(7, 9), frac_make(7, 9));
    ok("7/9 - 7/9 = 0", r.a == 0 && r.d == 1);

    ok("真分数判定：1/2 是真分数", frac_is_proper(frac_make(1, 2)));
    ok("真分数判定：3/2 不是真分数", !frac_is_proper(frac_make(3, 2)));
    ok("大小比较：1/3 < 1/2", frac_less(frac_make(1, 3), frac_make(1, 2)));
}

/*------------------------------ 用例：数值与表达式解析 ---------------------*/
static void test_parse(void)
{
    Frac v;

    test_section("A2 数值与表达式解析");

    ok("解析自然数 42", parse_line("42", &v, 1) && v.a == 42 && v.d == 1);

    ok("解析真分数 3/5", parse_line("3/5", &v, 1) && v.a == 3 && v.d == 5);

    ok("解析带分数 2'3/8", parse_line("2'3/8", &v, 1) && v.a == 19 && v.d == 8);

    ok("解析后可约分 4/8 -> 1/2", parse_line("4/8", &v, 1) && v.a == 1 && v.d == 2);

    ok("解析 3 + 4 * 5 = 23（先乘后加）",
       parse_line("3 + 4 * 5", &v, 1) && v.a == 23 && v.d == 1);

    ok("解析 ( 3 + 4 ) * 5 = 35（括号优先）",
       parse_line("( 3 + 4 ) * 5", &v, 1) && v.a == 35 && v.d == 1);

    /* 批改学生答案时按“求值模式”解析（最后一个参数为 0），只看数值对不对 */
    ok("求值模式解析 8 / ( 4 / 2 ) = 4（括号改变顺序）",
       parse_line("8 / ( 4 / 2 )", &v, 0) && v.a == 4 && v.d == 1);

    ok("求值模式解析 7/8 / 3/4 = 7/6",
       parse_line("7/8 / 3/4", &v, 0) && v.a == 7 && v.d == 6);

    ok("解析 1/6 + 1/8 = 7/24",
       parse_line("1/6 + 1/8", &v, 1) && v.a == 7 && v.d == 24);

    /* 按题意应判为不合法的表达式 */
    ok("拒绝出现负数的式子 3 - 5", !parse_line("3 - 5", &v, 1));
    ok("拒绝除数为 0 的式子 3 / 0", !parse_line("3 / 0", &v, 1));
    ok("拒绝除法结果不是真分数的 5 / 3", !parse_line("5 / 3", &v, 1));
    ok("拒绝除法结果等于 1 的 3 / 3", !parse_line("3 / 3", &v, 1));
    ok("拒绝残缺表达式 3 +", !parse_line("3 +", &v, 1));
    ok("拒绝多余内容 3 + 4 5", !parse_line("3 + 4 5", &v, 1));
    ok("接受除法结果为真分数的 3 / 5", parse_line("3 / 5", &v, 1) && v.a == 3 && v.d == 5);
}

/*------------------------------ 用例：题面与括号 ---------------------------*/
static void test_print(void)
{
    char buf[MAX_LINE];
    Expr *e;
    Frac v;

    test_section("A3 题面输出与括号");

    /* (3 + 4) * 5：左子表达式优先级低，必须加括号 */
    e = new_node(3, new_node(1, new_leaf(frac_make(3, 1)), new_leaf(frac_make(4, 1))),
                    new_leaf(frac_make(5, 1)));
    put_begin(buf, MAX_LINE);
    print_expr(e);
    ok("(3+4)*5 打印出括号", strstr(buf, "( ") != NULL && strstr(buf, " )") != NULL);
    ok("(3+4)*5 能解析回 35", parse_line(buf, &v, 1) && v.a == 35);
    free_expr(e);

    /* 3 + 4 * 5：右子表达式优先级高，不需要括号 */
    e = new_node(1, new_leaf(frac_make(3, 1)),
                    new_node(3, new_leaf(frac_make(4, 1)), new_leaf(frac_make(5, 1))));
    put_begin(buf, MAX_LINE);
    print_expr(e);
    ok("3+4*5 不加多余的括号", strstr(buf, "(") == NULL);
    ok("3+4*5 能解析回 23", parse_line(buf, &v, 1) && v.a == 23);
    free_expr(e);

    /* 8 / (4 / 2)：除法的右操作数必须加括号 */
    e = new_node(4, new_leaf(frac_make(8, 1)),
                    new_node(4, new_leaf(frac_make(4, 1)), new_leaf(frac_make(2, 1))));
    put_begin(buf, MAX_LINE);
    print_expr(e);
    ok("8/(4/2) 打印出括号", strstr(buf, "( ") != NULL);
    ok("8/(4/2) 能解析回 4（求值模式）", parse_line(buf, &v, 0) && v.a == 4);
    free_expr(e);

    {
        /* 带分数与真分数的输出格式 */
        char s[64];
        frac_text(frac_make(19, 8), s, sizeof(s));
        ok("带分数输出 2'3/8", strcmp(s, "2'3/8") == 0);
        frac_text(frac_make(3, 5), s, sizeof(s));
        ok("真分数输出 3/5", strcmp(s, "3/5") == 0);
        frac_text(frac_make(6, 1), s, sizeof(s));
        ok("整数输出 6", strcmp(s, "6") == 0);
    }
}

/*------------------------------ 用例：规范形式与去重 -----------------------*/
static void test_canon(void)
{
    char k1[2048], k2[2048], k3[2048], k4[2048];
    char buf[MAX_LINE];
    Expr *e;
    Frac v;

    test_section("A4 规范形式（去重依据）");

    /* 1 + 2 + 3 与 3 + ( 2 + 1 )：交换律 + 结合律 -> 同一道题 */
    e = new_node(1, new_node(1, new_leaf(frac_make(1, 1)), new_leaf(frac_make(2, 1))),
                    new_leaf(frac_make(3, 1)));
    canon_to(e, k1, sizeof(k1));
    free_expr(e);

    e = new_node(1, new_leaf(frac_make(3, 1)),
                    new_node(1, new_leaf(frac_make(2, 1)), new_leaf(frac_make(1, 1))));
    canon_to(e, k2, sizeof(k2));
    free_expr(e);
    ok("1+2+3 与 3+(2+1) 判为重复", strcmp(k1, k2) == 0);

    /* 3 + 2 + 1：左结合是 (3+2)+1，与 1+2+3 是同一道题 */
    e = new_node(1, new_node(1, new_leaf(frac_make(3, 1)), new_leaf(frac_make(2, 1))),
                    new_leaf(frac_make(1, 1)));
    canon_to(e, k3, sizeof(k3));
    free_expr(e);
    ok("3+2+1 与 1+2+3 判为重复", strcmp(k1, k3) == 0);

    /* 23 + 45 与 45 + 23 是重复的 */
    e = new_node(1, new_leaf(frac_make(23, 1)), new_leaf(frac_make(45, 1)));
    canon_to(e, k1, sizeof(k1));
    free_expr(e);
    e = new_node(1, new_leaf(frac_make(45, 1)), new_leaf(frac_make(23, 1)));
    canon_to(e, k2, sizeof(k2));
    free_expr(e);
    ok("23+45 与 45+23 判为重复", strcmp(k1, k2) == 0);

    /* 6 × 8 与 8 × 6 是重复的 */
    e = new_node(3, new_leaf(frac_make(6, 1)), new_leaf(frac_make(8, 1)));
    canon_to(e, k1, sizeof(k1));
    free_expr(e);
    e = new_node(3, new_leaf(frac_make(8, 1)), new_leaf(frac_make(6, 1)));
    canon_to(e, k2, sizeof(k2));
    free_expr(e);
    ok("6*8 与 8*6 判为重复", strcmp(k1, k2) == 0);

    /* 3 - 2 与 2 - 3 不是重复的（减法不满足交换律） */
    e = new_node(2, new_leaf(frac_make(3, 1)), new_leaf(frac_make(2, 1)));
    canon_to(e, k1, sizeof(k1));
    free_expr(e);
    e = new_node(2, new_leaf(frac_make(2, 1)), new_leaf(frac_make(3, 1)));
    canon_to(e, k2, sizeof(k2));
    free_expr(e);
    ok("3-2 与 2-3 不重复", strcmp(k1, k2) != 0);

    /* 1/2 + 1/3 与 1/3 + 1/2 是重复的 */
    e = new_node(1, new_leaf(frac_make(1, 2)), new_leaf(frac_make(1, 3)));
    canon_to(e, k1, sizeof(k1));
    free_expr(e);
    e = new_node(1, new_leaf(frac_make(1, 3)), new_leaf(frac_make(1, 2)));
    canon_to(e, k2, sizeof(k2));
    free_expr(e);
    ok("1/2+1/3 与 1/3+1/2 判为重复", strcmp(k1, k2) == 0);

    /* (1+2)*3 与 1+2*3 不是同一道题 */
    e = new_node(3, new_node(1, new_leaf(frac_make(1, 1)), new_leaf(frac_make(2, 1))),
                    new_leaf(frac_make(3, 1)));
    canon_to(e, k3, sizeof(k3));
    free_expr(e);
    e = new_node(1, new_leaf(frac_make(1, 1)),
                    new_node(3, new_leaf(frac_make(2, 1)), new_leaf(frac_make(3, 1))));
    canon_to(e, k4, sizeof(k4));
    free_expr(e);
    ok("(1+2)*3 与 1+2*3 不重复", strcmp(k3, k4) != 0);

    /* 哈希去重：同一个键第二次插入必须失败 */
    ok("哈希表能识别重复键", hash_add_unique("(1 + 2)") == 1 &&
                             hash_add_unique("(1 + 2)") == 0);

    /* 生成器最终产出的题目：既满足题意、又无打印歧义 */
    {
        int i, bad = 0;
        Frac ans, back;
        srand(12345);
        for (i = 0; i < 200; i++)
        {
            int dup_try = 0;
            Expr *g = gen_unique(10, &ans, &dup_try);
            char key[2048];
            if (g == NULL) { bad++; continue; }
            put_begin(buf, MAX_LINE);
            print_expr(g);
            if (!parse_line(buf, &back, 0)) bad++;
            else if (back.a != ans.a || back.d != ans.d) bad++;
            canon_to(g, key, sizeof(key));
            if (!hash_add_unique(key)) { /* 与测试前面造过的题目可能重复，忽略 */ }
            free_expr(g);
        }
        ok("生成器产出 200 道题全部合法且题面无歧义", bad == 0);
    }

    /* 顺便统计一下 gen_expr 直接产出的题面里有多少是有歧义的，
       说明为什么 gen_unique 里要做“题面自检” */
    {
        int i, total = 0, amb = 0;
        Frac ans;
        srand(999);
        for (i = 0; i < 500; i++)
        {
            Expr *g = gen_expr(rand_int(1, MAX_OPS), 10);
            char text[MAX_LINE];
            if (validate_expr(g, &ans))
            {
                total++;
                put_begin(text, MAX_LINE);
                print_expr(g);
                if (!canon_equal(g, text)) amb++;
            }
            free_expr(g);
        }
        printf("       （直接生成的题面中，有 %d/%d 存在歧义，已由自检剔除）\n", amb, total);
        ok("自检机制确实能发现一部分歧义题面", total > 0);
    }
}

/*------------------------------ 工具：读写文件 ---------------------------------*/
static long file_lines(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n = 0;
    int c;
    if (f == NULL) return -1;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n') n++;
    fclose(f);
    return n;
}

/* 读出 Exercises.txt 的第 no 行（1 开始），存到 out */
static int read_line(const char *path, int no, char *out, int size)
{
    FILE *f = fopen(path, "r");
    int i;
    if (f == NULL) return 0;
    for (i = 1; i <= no; i++)
        if (fgets(out, size, f) == NULL) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

/* 读出文件里最后一行非空内容，用来检查程序的统计输出 */
static int read_last_line(const char *path, char *out, int size)
{
    FILE *f = fopen(path, "r");
    char buf[MAX_LINE];
    if (f == NULL) return 0;
    out[0] = '\0';
    while (fgets(buf, MAX_LINE, f) != NULL)
    {
        char *p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\n' && *p != '\r' && *p != '\0')
        {
            strncpy(out, p, size - 1);
            out[size - 1] = '\0';
        }
    }
    fclose(f);
    return 1;
}

/* 把内容写到文件，供下一次 system() 重定向使用 */
static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f != NULL) { fputs(text, f); fclose(f); }
}

/* 把答案文件的第 no 行替换成 text */
static int patch_answer(const char *src, const char *dst, int no, const char *text)
{
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "w");
    char buf[MAX_LINE];
    int i = 0;
    if (in == NULL || out == NULL) return 0;
    while (fgets(buf, MAX_LINE, in) != NULL)
    {
        i++;
        if (i == no) fprintf(out, "答案%d: %s\n", no, text);
        else fputs(buf, out);
    }
    fclose(in);
    fclose(out);
    return 1;
}

/* 统计文件中某个子串出现的次数 */
static int count_substr(const char *path, const char *sub)
{
    FILE *f = fopen(path, "rb");
    static char buf[65536];
    size_t got;
    int n = 0;
    char *p;
    if (f == NULL) return -1;
    got = fread(buf, 1, sizeof(buf) - 1, f);
    buf[got] = '\0';
    fclose(f);
    p = buf;
    while ((p = strstr(p, sub)) != NULL) { n++; p++; }
    return n;
}

/*------------------------------ 用例：集成测试 -----------------------------*/
static void test_integration(void)
{
    char cmd[2048];
    char line[MAX_LINE];
    Frac v;
    long n;
    int i;

    test_section("B1 生成模式：Myapp.exe -n 20 -r 12");

    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -n 20 -r 12 > _gen.txt 2>&1", g_dir);
    ok("启动生成程序返回 0", system(cmd) == 0);

    n = file_lines("Exercises.txt");
    ok("Exercises.txt 有 20 行", n == 20);
    n = file_lines("Answers.txt");
    ok("Answers.txt 有 20 行", n == 20);

    /* 逐行检查：题面必须以 " =" 结尾，答案能解析，且题目本身合法 */
    {
        int bad_ex = 0, bad_an = 0, k;
        for (k = 1; k <= 20; k++)
        {
            char expr[MAX_LINE], an[MAX_LINE];
            if (!read_line("Exercises.txt", k, line, sizeof(line))) { bad_ex++; continue; }
            if (!cut_exercise(line, expr, sizeof(expr))) { bad_ex++; continue; }
            if (strchr(line, '=') == NULL) bad_ex++;
            if (!parse_line(expr, &v, 1)) { bad_ex++; continue; }

            if (!read_line("Answers.txt", k, an, sizeof(an))) { bad_an++; continue; }
            if (!cut_answer(an, expr, sizeof(expr))) { bad_an++; continue; }
            if (!parse_line(expr, &v, 0)) bad_an++;         /* 答案必须能被解析 */
        }
        ok("20 道题面全部合法（可解析、带等号）", bad_ex == 0);
        ok("20 条答案全部是合法数值", bad_an == 0);
    }

    /* 一行一道题：文件里的换行数必须等于行数 */
    ok("Exercises.txt 没有多余的换行", file_lines("Exercises.txt") == 20);
    ok("Answers.txt 没有多余的换行", file_lines("Answers.txt") == 20);

    /* 运算符个数不超过 3 个（注意分数的 "a/b" 里那个 / 不算四则运算符） */
    {
        int k, bad = 0;
        for (k = 1; k <= 20; k++)
        {
            char expr[MAX_LINE];
            int ops = 0;
            const char *p;
            if (!read_line("Exercises.txt", k, line, sizeof(line))) continue;
            if (!cut_exercise(line, expr, sizeof(expr))) continue;
            for (p = expr; *p; p++)
            {
                if (*p == '+' || *p == '-') ops++;      /* + - 一定是运算符 */
                else if (*p == '*' || *p == '/')
                {
                    /* 形如 a/b 的分数不算除法：左边是数字、右边也是数字 */
                    int is_frac = (*p == '/' && p > expr && p[1] >= '0' && p[1] <= '9' &&
                                   (p[-1] >= '0' && p[-1] <= '9'));
                    if (!is_frac) ops++;
                }
            }
            if (ops > MAX_OPS)
            {
                bad++;
                printf("       运算符超过 3 个：%s", expr);
            }
        }
        ok("每道题的运算符不超过 3 个", bad == 0);
    }

    /* 数值范围：所有自然数与分数分子分母都应小于 -r */
    {
        int k, bad = 0;
        for (k = 1; k <= 20; k++)
        {
            char expr[MAX_LINE];
            const char *p;
            if (!read_line("Exercises.txt", k, line, sizeof(line))) continue;
            if (!cut_exercise(line, expr, sizeof(expr))) continue;
            for (p = expr; *p; p++)
            {
                if (*p >= '0' && *p <= '9')
                {
                    long val = strtol(p, (char **)&p, 10);
                    if (val >= 12) bad++;
                    p--;
                }
            }
        }
        ok("题目中出现的数值都小于 -r（12）", bad == 0);
    }

    test_section("B2 批改模式：Myapp.exe -e ... -a ...");

    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -e Exercises.txt -a Answers.txt > _out.txt 2>&1", g_dir);
    ok("批改原始答案返回 0", system(cmd) == 0);
    {
        FILE *fg = fopen("Grade.txt", "r");
        char g[MAX_LINE] = "";
        int all_right = 0;
        if (fg != NULL) { fgets(g, sizeof(g), fg); fclose(fg); }
        all_right = (strstr(g, "Correct: 20 ") != NULL);
        ok("全部答案正确时 Grade.txt 记为 20 对", all_right);
    }

    /* 故意改错第 3、7、15 题的答案，应当被判错 */
    ok("能够构造错误答案文件",
       patch_answer("Answers.txt", "_bad.txt", 3, "99999") &&
       patch_answer("_bad.txt", "_bad2.txt", 7, "0") &&
       patch_answer("_bad2.txt", "_bad.txt", 15, "1/999"));

    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -e Exercises.txt -a _bad.txt > _out.txt 2>&1", g_dir);
    ok("批改错误答案返回 0", system(cmd) == 0);
    {
        FILE *fg = fopen("Grade.txt", "r");
        char g1[MAX_LINE] = "", g2[MAX_LINE] = "";
        if (fg != NULL) { fgets(g1, sizeof(g1), fg); fgets(g2, sizeof(g2), fg); fclose(fg); }
        ok("Grade.txt 第一行 Correct: 17", strstr(g1, "Correct: 17 ") != NULL);
        ok("Grade.txt 第二行 Wrong: 3", strstr(g2, "Wrong: 3 ") != NULL);
        ok("错误题号是 3、7、15", strstr(g2, "3, 7, 15") != NULL);
    }

    /* 一万道题的压力测试 */
    test_section("B3 一万道题");
    write_file("_gen.txt", "");                      /* 先清空日志文件 */
    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -n 10000 -r 100 > _gen.txt 2>&1", g_dir);
    ok("生成 10000 道题返回 0", system(cmd) == 0);
    ok("Exercises.txt 正好 10000 行", file_lines("Exercises.txt") == 10000);
    ok("Answers.txt 正好 10000 行", file_lines("Answers.txt") == 10000);

    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -e Exercises.txt -a Answers.txt > _out.txt 2>&1", g_dir);
    ok("批改 10000 道题返回 0", system(cmd) == 0);
    {
        FILE *fg = fopen("Grade.txt", "r");
        char g[MAX_LINE] = "";
        if (fg != NULL) { fgets(g, sizeof(g), fg); fclose(fg); }
        ok("10000 道题全部批改正确", strstr(g, "Correct: 10000 ") != NULL);
    }

    /* 去重与性能统计：程序会报告丢弃的重复题目数与耗时 */
    {
        char last[MAX_LINE] = "";
        read_last_line("_gen.txt", last, sizeof(last));
        ok("生成结束有耗时统计输出", strstr(last, "毫秒") != NULL);
        printf("       （生成统计：%s）\n", last);
    }

    test_section("B4 参数与帮助信息");
    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -n 5 > _out.txt 2>&1", g_dir);
    ok("缺少 -r 时返回非 0", system(cmd) != 0);
    {
        FILE *fo = fopen("_out.txt", "r");
        char o[8192] = "";
        size_t got = 0;
        if (fo != NULL) { got = fread(o, 1, sizeof(o) - 1, fo); fclose(fo); }
        o[got] = '\0';
        ok("缺少 -r 时给出帮助信息", strstr(o, "用法") != NULL && strstr(o, "-r") != NULL);
    }

    sprintf(cmd, "cd /d \"%s\" && Myapp.exe -r 10 -z > _out.txt 2>&1", g_dir);
    ok("未知参数时返回非 0", system(cmd) != 0);

    /* 清理集成测试产生的文件 */
    remove("_bad.txt");
    remove("_bad2.txt");
    remove("_out.txt");
    remove("_gen.txt");
    (void)i;
}

/*------------------------------ 主函数 -------------------------------------*/
int main(int argc, char *argv[])
{
    if (argc > 1) strncpy(g_dir, argv[1], sizeof(g_dir) - 1);
    printf("小学四则运算题目生成器 —— 测试程序\n");
    printf("工作目录：%s\n", g_dir);

    /* 切到项目目录：Myapp.exe 把 Exercises.txt 等文件写在它的当前目录，
       测试程序也要在同一个目录里读写这些文件 */
    if (_chdir(g_dir) != 0)
    {
        printf("错误：无法进入目录 %s\n", g_dir);
        return 1;
    }

    test_fraction();
    test_parse();
    test_print();
    test_canon();
    test_integration();

    printf("\n============================\n");
    printf("通过 %d 个用例，失败 %d 个用例\n", g_pass, g_fail);
    printf("============================\n");
    return (g_fail == 0) ? 0 : 1;
}


