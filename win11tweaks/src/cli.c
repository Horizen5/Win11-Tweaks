/* ============================================================================
 *  cli.c  --  命令行界面
 *  ----------------------------------------------------------------------------
 *  设计原则：所有“会改系统”的动作都先算差异、再确认、再自动备份，最后才写。
 * ========================================================================== */
#include "wt.h"

extern int g_argc;
extern WCHAR **g_argv;

static int g_noElevate = 0;

/* ---------------------------------------------------------------- 小工具 */

static void Banner(void)
{
    ConWriteF(L"\n%ls  v%ls   ——   原生 C 单文件版\n", WTA_NAME, WTA_VER);
    ConWrite(L"============================================================\n\n");
}

static void RefreshIconCache(void)
{
    WCHAR sys[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH * 2];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    GetSystemDirectoryW(sys, MAX_PATH);
    wt_joinPath(exe, MAX_PATH, sys, L"ie4uinit.exe");
    if (!wt_fileExists(exe)) return;
    wt_strlcpy(cmd, L"\"", MAX_PATH * 2);
    wt_strlcat(cmd, exe, MAX_PATH * 2);
    wt_strlcat(cmd, L"\" -ClearIconCache", MAX_PATH * 2);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static WCHAR *JoinArgs(int argc, WCHAR **argv, int start)
{
    StrBuf sb;
    int i;
    sbInit(&sb);
    for (i = start; i < argc; i++) {
        if (sb.len) sbAddCh(&sb, L' ');
        if (wcschr(argv[i], L' ')) {
            sbAddCh(&sb, L'"');
            sbAdd(&sb, argv[i]);
            sbAddCh(&sb, L'"');
        } else {
            sbAdd(&sb, argv[i]);
        }
    }
    return sbDetach(&sb);
}

static WCHAR *DefaultOutPath(const Ctx *ctx, const WCHAR *prefix)
{
    WCHAR stamp[64], name[MAX_PATH], *out;
    wt_nowStamp(stamp, 64);
    _snwprintf_s(name, MAX_PATH, _TRUNCATE, L"%ls_%ls.json", prefix, stamp);
    out = (WCHAR *)wt_alloc(sizeof(WCHAR) * (MAX_PATH * 2));
    wt_joinPath(out, MAX_PATH * 2, ctx->exeDir, name);
    return out;
}

BOOL MakeBackupProxy(const Ctx *ctx, const Profile *cur, WCHAR *outPath, int cch)
{
    WCHAR dir[MAX_PATH * 2], stamp[64], name[128];
    wt_joinPath(dir, MAX_PATH * 2, ctx->exeDir, L"backups");
    wt_mkdirTree(dir);
    wt_nowStamp(stamp, 64);
    _snwprintf_s(name, 128, _TRUNCATE, L"backup_%ls.json", stamp);
    wt_joinPath(outPath, cch, dir, name);
    return ProfileSaveJson(cur, outPath);
}

/* ---------------------------------------------------------------- 差异展示 */

static int ShowDiff(const Profile *target, const Profile *cur, const DiffItem *d, int n)
{
    int i, cnt = 0;
    ConWrite(L"  序号  开关                        注册表值                     当前值  ->  配置值\n");
    ConWrite(L"  --------------------------------------------------------------------------------\n");
    for (i = 0; i < n; i++) {
        const RegOp *op;
        const TweakDef *td;
        WCHAR a[128], b[128];
        const WCHAR *mark;
        if (d[i].kind == DIFF_SAME) continue;
        op = &g_ops[d[i].opIdx];
        td = &g_tweaks[op->tweak];
        ValToText(&cur->vals[d[i].opIdx], a, 128);
        ValToText(&target->vals[d[i].opIdx], b, 128);
        mark = (d[i].kind == DIFF_ADD) ? L"新增" :
               (d[i].kind == DIFF_DEL) ? L"删除" : L"变更";
        ConWriteF(L"  %4d  %-24ls  %-26ls  %ls  %ls  %ls\n",
                  d[i].opIdx + 1, td->label,
                  op->name ? op->name : L"(默认值)",
                  a, mark, b);
        cnt++;
    }
    if (!cnt) ConWrite(L"  （没有差异，当前系统已经和配置文件一致）\n");
    ConWrite(L"\n");
    return cnt;
}

static int AnyAdminDiff(const Profile *target, const DiffItem *d, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (d[i].kind != DIFF_SAME && (g_ops[d[i].opIdx].flags & OPF_ADMIN)) return 1;
    (void)target;
    return 0;
}

/* ---------------------------------------------------------------- 帮助 */

static void Usage(void)
{
    ConWrite(L"用法：Win11Tweaks.exe [命令] [参数]\n\n");
    ConWrite(L"  不带任何参数        打开图形界面\n\n");
    ConWrite(L"命令：\n");
    ConWrite(L"  -a, --apply           按 settings.ini 应用设置（默认行为）\n");
    ConWrite(L"  -e, --export [文件]   读取当前系统状态，导出为配置文件（默认写到程序目录）\n");
    ConWrite(L"  -i, --import <文件>   导入配置文件并恢复（先出差异、再备份、最后写入）\n");
    ConWrite(L"  -d, --diff <文件>     只显示“当前系统 vs 配置文件”的差异，不写任何东西\n");
    ConWrite(L"  -s, --status          列出全部 51 个受管注册表值的当前状态\n");
    ConWrite(L"      --backup          只把当前状态备份成还原点\n");
    ConWrite(L"  -r, --restore <文件>  从某个还原点文件恢复（同样先出差异）\n");
    ConWrite(L"      --ini <文件>      指定 settings.ini 的路径\n");
    ConWrite(L"  -h, --help            显示本帮助\n");
    ConWrite(L"      --version         显示版本\n\n");
    ConWrite(L"通用选项：\n");
    ConWrite(L"  -y, --yes             不再询问，直接执行\n");
    ConWrite(L"  -n, --dry-run         只报告将要做什么，不真正写入\n\n");
}

/* ---------------------------------------------------------------- 主流程 */

static int CmdApply(Ctx *ctx, const IniFile *ini)
{
    Profile *before, *after;
    Report rep;
    int i, changed = 0;

    ConWrite(L"[应用] 按 settings.ini 刷新系统设置...\n\n");

    /* 去箭头 / 去盾牌要写 HKLM。需要提权时自动重新拉起自己（可用 --no-elevate 关掉）。 */
    if (!ctx->admin && !g_noElevate && !ctx->dryRun &&
        (IniBool(ini, L"Desktop", L"HideShortcutArrow", 1) ||
         IniBool(ini, L"Desktop", L"HideUACShield", 1))) {
        WCHAR *a = JoinArgs(g_argc, g_argv, 1);
        ConWrite(L"[提权] 去箭头 / 去盾牌需要管理员权限，正在以管理员身份重新启动...\n");
        {
            BOOL r = wt_relaunchAsAdmin(a);
            free(a);
            return r ? 3 : 1;
        }
    }

    before = TweakSnapshot();

    TweakApplyAll(ctx, ini, &rep);

    after = TweakSnapshot();
    for (i = 0; i < g_opCount; i++) {
        const RegOp *op = &g_ops[i];
        ValState *a = &before->vals[i], *b = &after->vals[i];
        int same;
        if (a->exists != b->exists) same = 0;
        else if (!a->exists) same = 1;
        else if (a->type == REG_DWORD) same = (a->dw == b->dw);
        else if (a->type == REG_BINARY) same = (a->binLen == b->binLen && (a->binLen == 0 || memcmp(a->bin, b->bin, a->binLen) == 0));
        else same = (a->sz && b->sz && wt_ieq(a->sz, b->sz));
        if (!same) {
            (void)op;
            changed++;
        }
    }

    ConWriteF(L"\n完成：写入 %d 项 / 跳过 %d 项 / 失败 %d 项（%d 项实际发生变化）\n",
              rep.applied, rep.skipped, rep.failed, changed);

    if (!ctx->dryRun &&
        (IniBool(ini, L"Desktop", L"HideShortcutArrow", 1) ||
         IniBool(ini, L"Desktop", L"HideUACShield", 1))) {
        RefreshIconCache();
        ConWrite(L"已刷新图标缓存\n");
    }
    ProfileFree(before);
    ProfileFree(after);
    return rep.failed > 0 ? 1 : 0;
}

static int CmdExport(Ctx *ctx, const WCHAR *path)
{
    Profile *pf = TweakSnapshot();
    WCHAR *out = path ? wt_strdup(path) : DefaultOutPath(ctx, L"profile");
    BOOL ok;
    int i, exist = 0;

    for (i = 0; i < pf->count; i++) if (pf->vals[i].exists) exist++;

    ConWrite(L"[导出] 正在读取当前系统状态...\n");
    ConWriteF(L"  受管注册表值 %d 个，其中本机实际存在 %d 个\n", pf->count, exist);

    ok = ProfileSaveJson(pf, out);
    if (ok) {
        ConWriteF(L"\n已导出到：%ls\n", out);
        ConWriteF(L"  导出机器：%ls\\%ls（%ls）\n", pf->machine, pf->user, pf->os);
        ConWrite(L"  换新电脑后，运行  Win11Tweaks.exe --import \"该文件\"  即可恢复。\n");
    } else {
        ConWriteF(L"\n[失败] 无法写入 %ls\n", out);
    }
    free(out);
    ProfileFree(pf);
    return ok ? 0 : 1;
}

static int CmdImport(Ctx *ctx, const WCHAR *path, int skipConfirm, int doBackup)
{
    WCHAR err[256];
    Profile *target, *cur;
    DiffItem *d = NULL;
    int n, cnt, i;
    WCHAR bkPath[MAX_PATH * 2];
    Report rep;

    if (!path) { ConWrite(L"[错误] 请用 --import <文件> 指定配置文件\n"); return 2; }

    ConWriteF(L"[导入] 读取配置：%ls\n", path);
    target = ProfileLoadJson(path, err, 256);
    if (!target) { ConWriteF(L"[错误] %ls\n", err[0] ? err : L"解析失败"); return 2; }

    ConWriteF(L"  来源机器：%ls\\%ls（%ls）\n", target->machine, target->user, target->os);
    ConWriteF(L"  导出时间：%ls\n\n", target->exported);

    cur = TweakSnapshot();
    n = ProfileDiff(target, cur, &d);

    ConWrite(L"[差异] 当前系统  与  配置文件  的不同：\n\n");
    cnt = ShowDiff(target, cur, d, n);
    if (cnt == 0) {
        ConWrite(L"无需恢复，当前系统已经一致。\n");
        free(d); ProfileFree(target); ProfileFree(cur);
        return 0;
    }

    if (ctx->dryRun) {
        ConWrite(L"[dry-run] 以上为将要执行的改动，未做任何写入。\n");
        free(d); ProfileFree(target); ProfileFree(cur);
        return 0;
    }

    if (AnyAdminDiff(target, d, n) && !ctx->admin) {
        WCHAR *a = JoinArgs(g_argc, g_argv, 1);
        ConWrite(L"\n[提权] 有项目需要管理员权限，正在重新以管理员身份启动...\n");
        free(d); ProfileFree(target); ProfileFree(cur);
        {
            BOOL r = wt_relaunchAsAdmin(a);
            free(a);
            return r ? 3 : 1;   /* 3 = 已重新拉起，调用方直接退出 */
        }
    }

    if (!skipConfirm) {
        if (!ConYes(L"\n确认按上面的清单恢复？")) {
            ConWrite(L"已取消。\n");
            free(d); ProfileFree(target); ProfileFree(cur);
            return 0;
        }
    }

    if (doBackup) {
        if (MakeBackupProxy(ctx, cur, bkPath, MAX_PATH * 2))
            ConWriteF(L"\n[备份] 当前状态已存为还原点：\n        %ls\n", bkPath);
        else
            ConWrite(L"\n[警告] 还原点写入失败，但继续执行。\n");
    }

    ConWrite(L"\n[恢复] 开始写入...\n");
    TweakApplyStates(ctx, target, d, n, &rep);
    ConWriteF(L"\n完成：写入 %d 项 / 跳过 %d 项 / 失败 %d 项 / 无需改动 %d 项\n",
              rep.applied, rep.skipped, rep.failed, rep.unchanged);

    /* 若涉及图标角标，刷一次缓存 */
    for (i = 0; i < n; i++) {
        if (d[i].kind == DIFF_SAME) continue;
        if (g_ops[d[i].opIdx].tweak == T_NO_ARROW || g_ops[d[i].opIdx].tweak == T_NO_SHIELD) {
            RefreshIconCache();
            ConWrite(L"已刷新图标缓存\n");
            break;
        }
    }

    free(d);
    ProfileFree(target);
    ProfileFree(cur);
    return rep.failed > 0 ? 1 : 0;
}

static int CmdStatus(void)
{
    Profile *pf = TweakSnapshot();
    int i, exist = 0;
    ConWrite(L"  序号  开关                        注册表值                              当前值\n");
    ConWrite(L"  ----------------------------------------------------------------------------------\n");
    for (i = 0; i < g_opCount; i++) {
        const RegOp *op = &g_ops[i];
        const TweakDef *td = &g_tweaks[op->tweak];
        WCHAR v[128];
        ValToText(&pf->vals[i], v, 128);
        if (pf->vals[i].exists) exist++;
        ConWriteF(L"  %4d  %-24ls  %-32ls  %ls\n", i + 1, td->label,
                  op->name ? op->name : L"(默认值)", v);
    }
    ConWriteF(L"\n共 %d 个受管注册表值，本机存在 %d 个。\n", pf->count, exist);
    ProfileFree(pf);
    return 0;
}

/* ---------------------------------------------------------------- 入口 */

int CliMain(const Ctx *ctx0, int argc, WCHAR **argv)
{
    Ctx ctx = *ctx0;
    const WCHAR *cmd = NULL, *path = NULL, *iniPath = NULL;
    const WCHAR *unknown = NULL;
    int skipConfirm = 0, doBackup = 1;
    int i, rc = 0;
    IniFile ini;

    /* 先扫一遍通用选项，再确定命令 */
    for (i = 1; i < argc; i++) {
        const WCHAR *a = argv[i];
        if (wt_ieq(a, L"-y") || wt_ieq(a, L"--yes")) { skipConfirm = 1; continue; }
        if (wt_ieq(a, L"-n") || wt_ieq(a, L"--dry-run")) { ctx.dryRun = TRUE; continue; }
        if (wt_ieq(a, L"--no-elevate")) { g_noElevate = 1; continue; }
        if (wt_ieq(a, L"--no-backup")) { doBackup = 0; continue; }
        if (wt_ieq(a, L"--ini")) { if (i + 1 < argc) iniPath = argv[++i]; continue; }
    }

    /* 找命令 */
    for (i = 1; i < argc; i++) {
        const WCHAR *a = argv[i];
        if (wt_ieq(a, L"-h") || wt_ieq(a, L"--help") || wt_ieq(a, L"/?")) { Banner(); Usage(); return 0; }
        if (wt_ieq(a, L"--version")) { WtTrace("cli.version"); ConWriteF(L"%ls %ls\n", WTA_NAME, WTA_VER); return 0; }
        if (wt_ieq(a, L"-a") || wt_ieq(a, L"--apply")) { cmd = L"apply"; break; }
        if (wt_ieq(a, L"-e") || wt_ieq(a, L"--export")) { cmd = L"export"; break; }
        if (wt_ieq(a, L"-i") || wt_ieq(a, L"--import")) { cmd = L"import"; break; }
        if (wt_ieq(a, L"-d") || wt_ieq(a, L"--diff")) { cmd = L"diff"; break; }
        if (wt_ieq(a, L"-r") || wt_ieq(a, L"--restore")) { cmd = L"restore"; break; }
        if (wt_ieq(a, L"-s") || wt_ieq(a, L"--status")) { cmd = L"status"; break; }
        if (wt_ieq(a, L"--backup")) { cmd = L"backup"; break; }
        if (wt_ieq(a, L"--gui") || wt_ieq(a, L"-g")) { cmd = L"gui"; break; }
        if (a[0] != L'-') { cmd = L"import"; path = a; break; }   /* 直接给文件名 -> 当成导入 */
        unknown = a;                                              /* 认不出的选项 */
        break;
    }

    /* ★ 安全阀：命令行里出现了认不出的选项时，绝不允许退化成“默认应用设置”。
      （曾经因为参数写错而静默改了整个系统的外观，这类事故必须从结构上堵死。） */
    if (!cmd && unknown) {
        ConWriteF(L"[错误] 无法识别的选项：%ls\n\n", unknown);
        Usage();
        return 2;
    }

    /* 命令后面的路径参数 */
    if (cmd) {
        int at = -1;
        for (i = 1; i < argc; i++) {
            const WCHAR *a = argv[i];
            if (wt_ieq(a, L"-a") || wt_ieq(a, L"--apply") || wt_ieq(a, L"-e") || wt_ieq(a, L"--export") ||
                wt_ieq(a, L"-i") || wt_ieq(a, L"--import") || wt_ieq(a, L"-d") || wt_ieq(a, L"--diff") ||
                wt_ieq(a, L"-r") || wt_ieq(a, L"--restore")) { at = i; break; }
            if (a[0] != L'-') { at = i - 1; break; }
        }
        if (!path && at >= 0 && at + 1 < argc && argv[at + 1][0] != L'-') path = argv[at + 1];
    }

    Banner();
    ConWriteF(L"系统：%ls   权限：%ls\n\n",
              ctx.win11 ? L"Windows 11" : L"Windows 10 或更早",
              ctx.admin ? L"管理员" : L"普通用户");
    WtTrace("cli.banner");

    if (!cmd) cmd = L"apply";

    /* ------- 分派 ------- */
    if (wt_ieq(cmd, L"status")) { rc = CmdStatus(); WtTrace("cli.status.done"); }
    else if (wt_ieq(cmd, L"export")) { rc = CmdExport(&ctx, path); }
    else if (wt_ieq(cmd, L"diff")) {
        Profile *t, *c; DiffItem *d = NULL; int n; WCHAR err[256];
        if (!path) { ConWrite(L"[错误] 请用 --diff <文件> 指定配置文件\n"); return 2; }
        WtTrace("cli.diff.preload");
        t = ProfileLoadJson(path, err, 256);
        WtTrace("cli.diff.postload");
        if (!t) { ConWriteF(L"[错误] %ls\n", err[0] ? err : L"解析失败"); return 2; }
        c = TweakSnapshot();
        WtTrace("cli.diff.postsnap");
        n = ProfileDiff(t, c, &d);
        ConWriteF(L"[差异] %ls  vs  当前系统：\n\n", path);
        ShowDiff(t, c, d, n);
        WtTrace("cli.diff.postshow");
        free(d);          WtTrace("cli.tail.free_d");
        ProfileFree(t);   WtTrace("cli.tail.free_t");
        ProfileFree(c);   WtTrace("cli.tail.free_c");
        rc = 0;
    }
    else if (wt_ieq(cmd, L"backup")) {
        Profile *c = TweakSnapshot();
        WCHAR p[MAX_PATH * 2];
        if (MakeBackupProxy(&ctx, c, p, MAX_PATH * 2)) ConWriteF(L"已备份到：%ls\n", p);
        else { ConWrite(L"[失败] 无法写入还原点\n"); rc = 1; }
        ProfileFree(c);
    }
    else if (wt_ieq(cmd, L"import") || wt_ieq(cmd, L"restore")) {
        rc = CmdImport(&ctx, path, skipConfirm, doBackup);
        if (rc == 3) return 3;      /* 已重新以管理员启动 */
    }
    else if (wt_ieq(cmd, L"gui")) { rc = 4; }   /* 转交给图形界面 */
    else { /* apply */
        WCHAR p[MAX_PATH * 2];
        wt_joinPath(p, MAX_PATH * 2, ctx.exeDir, L"settings.ini");
        if (!iniPath) iniPath = p;
        IniLoad(iniPath, &ini);
        if (ini.n == 0)
            ConWriteF(L"[提示] 未找到 %ls，全部按默认值（开启）执行。\n\n", iniPath);
        else
            ConWriteF(L"[配置] %ls（%d 项）\n\n", iniPath, ini.n);
        rc = CmdApply(&ctx, &ini);
        IniFree(&ini);
    }

    if (rc == 3) return 3;          /* 已重新以管理员启动，本进程直接退出 */
    WtTrace("cli.preIsFresh");
    if (ConIsFresh()) ConPause();
    WtTrace("cli.returning");
    return rc;
}
