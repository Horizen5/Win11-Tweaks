/* ============================================================================
 *  gui.c  --  图形界面（原生 Win32，无第三方框架）
 *  ----------------------------------------------------------------------------
 *  · 勾选框状态 = 系统【当前真实状态】（打开界面时真读注册表）
 *  · 四个动作：应用设置 / 导出配置 / 导入恢复 / 还原点回滚
 *  · 与 CLI 共用同一套底层逻辑，行为完全一致
 * ========================================================================== */
#include "wt.h"
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")
/* 注意：依赖清单（视觉样式 / asInvoker / 高 DPI）由 src\app.manifest 提供，
   在 build.ps1 里通过 /MANIFESTINPUT + /MANIFEST:EMBED 显式嵌入，
   不再用 #pragma manifestdependency（避免两处重复声明产生冲突）。 */

#define IDC_CHK_BASE   1000
#define IDC_COMBO      2000
#define IDC_LOG        4000
#define IDC_BTN_APPLY  3001
#define IDC_BTN_EXPORT 3002
#define IDC_BTN_IMPORT 3003
#define IDC_BTN_ROLL   3004
#define IDC_BTN_RELOAD 3005
#define IDC_BTN_ELEV   3006

static HWND  g_hwnd, g_hLog, g_hCombo;
static HFONT g_font;
static int   g_dpi = 96;
static Ctx   g_ctx;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

/* ---------------------------------------------------------------- 日志 */

static void FlushLog(void)
{
    int len;
    if (!g_hLog || !g_logBuf.p) return;
    len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)g_logBuf.p);
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
    sbClear(&g_logBuf);
}

/* ---------------------------------------------------------------- 分组布局 */

typedef struct { const WCHAR *title; int first, count; } Group;

/* 与 g_tweaks 的顺序一致，按界面分栏 */
static const Group kCol0[] = {
    { L"任务栏 / 右键菜单", T_TASKBAR_ALIGN, 10 },
    { L"桌面图标 / 排列",   T_SHOW_THISPC,    9 },
};
static const Group kCol1[] = {
    { L"主题",              T_APPLY_THEME,    4 },
    { L"通知 / 建议",       T_NO_NOTIF_CENTER, 4 },
    { L"开始菜单「推荐」",   T_ST_RECENT_APPS, 3 },
};
static const Group kCol2[] = {
    { L"开始菜单文件夹",     T_F_SETTINGS,     9 },
};

static HWND MakeCheck(HWND parent, int x, int y, int w, int idx)
{
    HWND h = CreateWindowExW(0, L"BUTTON", g_tweaks[idx].label,
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             x, y, w, S(18), parent, (HMENU)(INT_PTR)(IDC_CHK_BASE + idx),
                             NULL, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

/* ---------------------------------------------------------------- 读取当前状态 */

static void ReloadFromSystem(int quiet)
{
    Profile *pf = TweakSnapshot();
    int i;

    for (i = 0; i < T_COUNT; i++) {
        HWND h;
        int v;
        if (g_tweaks[i].kind == TK_VIRTUAL) {
            /* 虚拟总开关（ApplyTheme）不对应任何注册表值，默认视为“开启”。
               注意：不能 continue 跳过，否则勾选框会一直停在未选中状态。 */
            h = GetDlgItem(g_hwnd, IDC_CHK_BASE + i);
            if (h) SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
            continue;
        }
        h = GetDlgItem(g_hwnd, IDC_CHK_BASE + i);
        if (!h) continue;
        v = TweakReadBool(pf, i);
        SendMessageW(h, BM_SETCHECK, (v == 1) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(h, TRUE);
        /* Win10 上不支持的项目置灰 */
        if (!g_ctx.win11 && (i == T_TASKBAR_ALIGN || i == T_WIN10_CTXMENU)) {
            EnableWindow(h, FALSE);
            SendMessageW(h, BM_SETCHECK, BST_UNCHECKED, 0);
        }
    }
    {
        int lv = TweakReadCombine(pf);
        SendMessageW(g_hCombo, CB_SETCURSEL, (WPARAM)(lv < 0 ? 0 : lv), 0);
    }
    if (!quiet) {
        int exist = 0;
        for (i = 0; i < pf->count; i++) if (pf->vals[i].exists) exist++;
        LogLine(L"已读取系统当前状态：%d 个受管值中，本机存在 %d 个。\r\n", pf->count, exist);
    }
    ProfileFree(pf);
}

/* ---------------------------------------------------------------- 动作 */

static void DoApply(void)
{
    IniFile ini;
    Report rep;
    int i;

    memset(&ini, 0, sizeof(ini));
    for (i = 0; i < T_COUNT; i++) {
        HWND h;
        int on;
        if (g_tweaks[i].kind == TK_VIRTUAL) continue;
        h = GetDlgItem(g_hwnd, IDC_CHK_BASE + i);
        on = h && (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
        IniAdd(&ini, g_tweaks[i].section, g_tweaks[i].id, on ? L"1" : L"0");
    }
    {
        int lv = (int)SendMessageW(g_hCombo, CB_GETCURSEL, 0, 0);
        const WCHAR *nm = (lv == 1) ? L"已满时" : (lv == 2) ? L"从不" : L"始终";
        IniAdd(&ini, L"Shell", L"TaskbarCombine", nm);
    }

    LogLine(L"—— 开始应用设置 ——\r\n");
    TweakApplyAll(&g_ctx, &ini, &rep);
    LogLine(L"完成：写入 %d 项 / 跳过 %d 项 / 失败 %d 项\r\n", rep.applied, rep.skipped, rep.failed);
    if (!g_ctx.admin) LogLine(L"提示：去箭头 / 去盾牌需要管理员权限，已跳过。\r\n");
    IniFree(&ini);
    FlushLog();
    ReloadFromSystem(1);
    MessageBoxW(g_hwnd, L"设置已应用。\n\n部分项目（任务栏、图标缓存）需要注销一次才完全生效。",
                L"完成", MB_OK | MB_ICONINFORMATION);
}

static void DoExport(void)
{
    OPENFILENAMEW ofn;
    WCHAR path[MAX_PATH * 2];
    Profile *pf;
    int i, exist = 0;

    path[0] = 0;
    {
        WCHAR stamp[64], nm[128];
        wt_nowStamp(stamp, 64);
        _snwprintf_s(nm, 128, _TRUNCATE, L"profile_%ls.json", stamp);
        wt_joinPath(path, MAX_PATH * 2, g_ctx.exeDir, nm);
    }

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"配置文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"导出系统设置为配置文件";
    if (!GetSaveFileNameW(&ofn)) return;

    pf = TweakSnapshot();
    for (i = 0; i < pf->count; i++) if (pf->vals[i].exists) exist++;
    if (ProfileSaveJson(pf, path)) {
        LogLine(L"[导出] 成功 -> %ls\r\n", path);
        LogLine(L"       受管值 %d 个，本机存在 %d 个；来源机器 %ls\\%ls\r\n",
                pf->count, exist, pf->machine, pf->user);
    } else {
        LogLine(L"[导出] 失败：无法写入 %ls\r\n", path);
    }
    ProfileFree(pf);
    FlushLog();
}

static void DoImport(int fromBackups)
{
    OPENFILENAMEW ofn;
    WCHAR path[MAX_PATH * 2];
    WCHAR err[256];
    Profile *target, *cur;
    DiffItem *d = NULL;
    int n, cnt = 0, i;
    WCHAR bk[MAX_PATH * 2];
    Report rep;
    StrBuf msg;

    path[0] = 0;
    if (fromBackups) wt_joinPath(path, MAX_PATH * 2, g_ctx.exeDir, L"backups");

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"配置文件 / 还原点 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = fromBackups ? L"选择要回滚到的还原点" : L"选择要导入的配置文件";
    if (!GetOpenFileNameW(&ofn)) return;

    target = ProfileLoadJson(path, err, 256);
    if (!target) {
        LogLine(L"[导入] 失败：%ls\r\n", err[0] ? err : L"解析失败");
        FlushLog();
        MessageBoxW(g_hwnd, err[0] ? err : L"解析失败", L"导入失败", MB_OK | MB_ICONERROR);
        return;
    }
    cur = TweakSnapshot();
    n = ProfileDiff(target, cur, &d);

    LogLine(L"[导入] %ls\r\n", path);
    LogLine(L"       来源：%ls\\%ls（%ls），导出时间 %ls\r\n",
            target->machine, target->user, target->os, target->exported);

    sbInit(&msg);
    sbAddF(&msg, L"来源机器：%ls\\%ls\n导出时间：%ls\n\n将要执行的改动：\n\n", target->machine, target->user, target->exported);
    for (i = 0; i < n; i++) {
        WCHAR a[128], b[128];
        if (d[i].kind == DIFF_SAME) continue;
        ValToText(&cur->vals[d[i].opIdx], a, 128);
        ValToText(&target->vals[d[i].opIdx], b, 128);
        sbAddF(&msg, L"· %ls  [%ls]\n    %ls  ->  %ls\n",
               g_tweaks[g_ops[d[i].opIdx].tweak].label,
               d[i].kind == DIFF_ADD ? L"新增" : d[i].kind == DIFF_DEL ? L"删除" : L"变更",
               a, b);
        cnt++;
        if (cnt >= 12) { sbAdd(&msg, L"……  （清单过长，此处仅列前 12 项）\n"); break; }
    }
    if (!cnt) sbAdd(&msg, L"（没有差异，当前系统已经与配置文件一致）");
    sbAddF(&msg, L"\n\n共 %d 项改动。\n\n是否执行恢复？（执行前会自动备份当前值）", cnt);

    if (cnt > 0) {
        if (MessageBoxW(g_hwnd, msg.p, L"确认恢复（差异预览）", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            LogLine(L"       已取消。\r\n");
            sbFree(&msg); free(d); ProfileFree(target); ProfileFree(cur); FlushLog();
            return;
        }
        if (MakeBackupProxy(&g_ctx, cur, bk, MAX_PATH * 2))
            LogLine(L"[备份] 当前状态已存为还原点：%ls\r\n", bk);

        LogLine(L"—— 开始恢复 ——\r\n");
        TweakApplyStates(&g_ctx, target, d, n, &rep);
        LogLine(L"完成：写入 %d 项 / 跳过 %d 项 / 失败 %d 项 / 无需改动 %d 项\r\n",
                rep.applied, rep.skipped, rep.failed, rep.unchanged);
    } else {
        LogLine(L"       无需恢复，当前系统已经一致。\r\n");
    }

    sbFree(&msg);
    free(d); ProfileFree(target); ProfileFree(cur);
    FlushLog();
    ReloadFromSystem(1);
    if (cnt > 0) MessageBoxW(g_hwnd, L"恢复完成。\n\n建议注销一次让所有项目生效。", L"完成", MB_OK | MB_ICONINFORMATION);
}

static void DoElevate(void)
{
    extern int g_argc;
    extern WCHAR **g_argv;
    /* 用“无参数”重新以管理员身份打开 —— 也就是再来一个 GUI 实例 */
    if (wt_relaunchAsAdmin(L"")) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    else MessageBoxW(g_hwnd, L"提权被取消。", L"提示", MB_OK | MB_ICONWARNING);
    (void)g_argv; (void)g_argc;
}

/* ---------------------------------------------------------------- 创建控件 */

static const WCHAR *kCombineItems[] = { L"始终（合并并隐藏标签）", L"任务已满时", L"从不（显示窗口标题）" };

static void BuildControls(HWND h)
{
    int colW = S(248), colGap = S(8), margin = S(10);
    int colX[3];
    int y, i, k;

    colX[0] = margin;
    colX[1] = margin + colW + colGap;
    colX[2] = margin + (colW + colGap) * 2;

    /* ---- 第 0 栏 ---- */
    y = margin;
    for (k = 0; k < 2; k++) {
        int gh;
        int n = kCol0[k].count;
        gh = (k == 0) ? S(22) + n * S(20) + S(28) : S(22) + n * S(20) + S(10);
        CreateWindowExW(0, L"BUTTON", kCol0[k].title, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                        colX[0], y, colW, gh, h, NULL, NULL, NULL);
        for (i = 0; i < n; i++)
            MakeCheck(h, colX[0] + S(12), y + S(22) + i * S(20), colW - S(20), kCol0[k].first + i);
        if (k == 0) {
            g_hCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                       colX[0] + S(12), y + S(22) + n * S(20) + S(2),
                                       colW - S(24), S(200), h, (HMENU)IDC_COMBO, NULL, NULL);
            SendMessageW(g_hCombo, WM_SETFONT, (WPARAM)g_font, TRUE);
            for (i = 0; i < 3; i++)
                SendMessageW(g_hCombo, CB_ADDSTRING, 0, (LPARAM)kCombineItems[i]);
            SendMessageW(g_hCombo, CB_SETCURSEL, 0, 0);
        }
        y += gh + colGap;
    }

    /* ---- 第 1 栏 ---- */
    y = margin;
    for (k = 0; k < 3; k++) {
        int n = kCol1[k].count;
        int gh = S(22) + n * S(20) + S(10);
        CreateWindowExW(0, L"BUTTON", kCol1[k].title, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                        colX[1], y, colW, gh, h, NULL, NULL, NULL);
        for (i = 0; i < n; i++)
            MakeCheck(h, colX[1] + S(12), y + S(22) + i * S(20), colW - S(20), kCol1[k].first + i);
        y += gh + colGap;
    }

    /* ---- 第 2 栏 ---- */
    y = margin;
    for (k = 0; k < 1; k++) {
        int n = kCol2[k].count;
        int gh = S(22) + n * S(20) + S(10);
        CreateWindowExW(0, L"BUTTON", kCol2[k].title, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                        colX[2], y, colW, gh, h, NULL, NULL, NULL);
        for (i = 0; i < n; i++)
            MakeCheck(h, colX[2] + S(12), y + S(22) + i * S(20), colW - S(20), kCol2[k].first + i);
        y += gh + colGap;
    }

    /* ---- 按钮行 ---- */
    {
        int by = S(478) + S(12);
        int bw = S(120), bh = S(30), bx = margin;
        const struct { int id; const WCHAR *t; int w; } btns[] = {
            { IDC_BTN_APPLY,  L"应用设置",         S(96) },
            { IDC_BTN_EXPORT, L"导出配置...",      S(104) },
            { IDC_BTN_IMPORT, L"导入恢复...",      S(104) },
            { IDC_BTN_ROLL,   L"还原点回滚...",     S(112) },
            { IDC_BTN_RELOAD, L"重新读取",         S(88) },
        };
        for (i = 0; i < 5; i++) {
            HWND hb = CreateWindowExW(0, L"BUTTON", btns[i].t,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      bx, by, btns[i].w, bh, h, (HMENU)(INT_PTR)btns[i].id, NULL, NULL);
            SendMessageW(hb, WM_SETFONT, (WPARAM)g_font, TRUE);
            bx += btns[i].w + S(6);
        }
        if (!g_ctx.admin) {
            HWND hb = CreateWindowExW(0, L"BUTTON", L"以管理员重新打开",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      bx, by, S(140), bh, h, (HMENU)(INT_PTR)IDC_BTN_ELEV, NULL, NULL);
            SendMessageW(hb, WM_SETFONT, (WPARAM)g_font, TRUE);
        }
    }

    /* ---- 日志区 ---- */
    g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                             margin, S(528), S(780) - margin * 2, S(96),
                             h, (HMENU)IDC_LOG, NULL, NULL);
    SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_font, TRUE);
}

/* ---------------------------------------------------------------- 窗口过程 */

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        break;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_BTN_APPLY)  { DoApply(); return 0; }
        if (id == IDC_BTN_EXPORT) { DoExport(); return 0; }
        if (id == IDC_BTN_IMPORT) { DoImport(0); return 0; }
        if (id == IDC_BTN_ROLL)   { DoImport(1); return 0; }
        if (id == IDC_BTN_RELOAD) { ReloadFromSystem(0); FlushLog(); return 0; }
        if (id == IDC_BTN_ELEV)   { DoElevate(); return 0; }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- 入口 */

int GuiMain(const Ctx *ctx)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT rc;
    int cw, ch;
    NONCLIENTMETRICSW ncm;
    HDC hdc;

    g_ctx = *ctx;

    hdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    if (g_dpi <= 0) g_dpi = 96;

    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"Win11TweaksWnd";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return 1;

    cw = S(780);
    ch = S(636);
    rc.left = 0; rc.top = 0; rc.right = cw; rc.bottom = ch;
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    g_hwnd = CreateWindowExW(0, L"Win11TweaksWnd",
                             WTA_NAME L"  v" WTA_VER L"   ——   Win11 / Win10 一键顺手设置",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return 1;

    g_guiMode = 1;
    BuildControls(g_hwnd);

    LogLine(L"%ls v%ls  |  %ls  |  %ls\r\n", WTA_NAME, WTA_VER,
            ctx->win11 ? L"Windows 11" : L"Windows 10",
            ctx->admin ? L"管理员权限" : L"普通权限");
    if (!ctx->admin)
        LogLine(L"提示：当前是普通权限，去箭头 / 去盾牌会被跳过。可点【以管理员重新打开】。\r\n");
    ReloadFromSystem(1);
    FlushLog();

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
