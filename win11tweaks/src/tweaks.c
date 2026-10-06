/* ============================================================================
 *  tweaks.c  --  开关元数据表 + 应用 / 快照
 *  ----------------------------------------------------------------------------
 *  39 个「开关」映射到 51 个「注册表值」。表是平铺的，导出/导入统一按值处理；
 *  只有「按 ini 应用」时需要特判 5 处组合项：
 *    1) TaskbarCombine  三档 -> TaskbarGlomLevel + MMTaskbarGlomLevel
 *    2) AutoArrange/AlignToGrid 两开关 -> 一个 FFlags
 *    3) Win10ContextMenu -> 建键 + 空字符串默认值
 *    4) StartFolders 九个开关 -> 一个 REG_BINARY VisiblePlaces
 *    5) ApplyTheme 虚拟总开关（本身不写注册表）
 * ========================================================================== */
#include "wt.h"

/* --------------------------------------------------------------- 路径常量 */
#define K_ADV    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
#define K_SRCH   L"Software\\Microsoft\\Windows\\CurrentVersion\\Search"
#define K_NP     L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel"
#define K_CP     L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu"
#define K_CDM    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager"
#define K_PERS   L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define K_EXPL   L"Software\\Policies\\Microsoft\\Windows\\Explorer"
#define K_PUSH   L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications"
#define K_NOTIF  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings"
#define K_QUIET  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\QuietHours"
#define K_DSK    L"Control Panel\\Desktop"
#define K_BAGS   L"Software\\Microsoft\\Windows\\Shell\\Bags\\1\\Desktop"
#define K_NAMING L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\NamingTemplates"
#define K_SICON  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons"
#define K_START  L"Software\\Microsoft\\Windows\\CurrentVersion\\Start"
#define K_UPE    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\UserProfileEngagement"
#define K_CTX    L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InProcServer32"

#define C_THISPC L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}"
#define C_RECYC  L"{645FF040-5081-101B-9F08-00AA002F954E}"
#define C_CPANEL L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}"

/* 缩写，纯粹为了表格好读 */
#define CU  HIVE_CU
#define LM  HIVE_LM
#define DW  REG_DWORD
#define SZ  REG_SZ
#define BN  REG_BINARY
#define D   REGV_DEL
#define ADM OPF_ADMIN
#define W11 OPF_WIN11

/* ============================================================ 开关定义 */
const TweakDef g_tweaks[T_COUNT] = {
/*  { id,                  section,            label,                    defOn, kind } */
    { L"TaskbarAlignLeft",      L"Shell",            L"任务栏图标靠左",              1, TK_BOOL },
    { L"TaskbarCombine",        L"Shell",            L"合并任务栏按钮并隐藏标签",      1, TK_ENUM },
    { L"HideSearchBox",         L"Shell",            L"隐藏任务栏搜索框",            1, TK_BOOL },
    { L"HideTaskView",          L"Shell",            L"隐藏任务视图按钮",            1, TK_BOOL },
    { L"HideWidgets",           L"Shell",            L"隐藏小组件（天气）",          1, TK_BOOL },
    { L"HideChat",              L"Shell",            L"隐藏“聊天”按钮",             1, TK_BOOL },
    { L"ShowSeconds",           L"Shell",            L"任务栏时钟显示秒",            1, TK_BOOL },
    { L"HideNotificationBell",  L"Shell",            L"隐藏托盘通知铃铛",            1, TK_BOOL },
    { L"ExplorerOpenThisPC",    L"Shell",            L"资源管理器默认打开“此电脑”",    1, TK_BOOL },
    { L"Win10ContextMenu",      L"Shell",            L"右键菜单换回 Win10 完整版",    1, TK_BOOL },

    { L"ShowThisPC",            L"Desktop",          L"桌面显示“此电脑”",           1, TK_BOOL },
    { L"ShowRecycleBin",        L"Desktop",          L"桌面显示“回收站”",           1, TK_BOOL },
    { L"ShowControlPanel",      L"Desktop",          L"桌面显示“控制面板”",         1, TK_BOOL },
    { L"AutoArrange",           L"Desktop",          L"桌面自动排列图标",            1, TK_BOOL },
    { L"AlignToGrid",           L"Desktop",          L"自动排列时对齐到网格",         1, TK_BOOL },
    { L"SortByName",            L"Desktop",          L"桌面按名称排序",              1, TK_BOOL },
    { L"HideShortcutArrow",     L"Desktop",          L"去掉快捷方式小箭头",          1, TK_BOOL },
    { L"HideUACShield",         L"Desktop",          L"去掉 UAC 盾牌角标",          1, TK_BOOL },
    { L"NoShortcutSuffix",      L"Desktop",          L"新建快捷方式不带“- 快捷方式”",   1, TK_BOOL },

    { L"ApplyTheme",            L"Theme",            L"应用主题段（总开关）",         1, TK_VIRTUAL },
    { L"SystemDark",            L"Theme",            L"任务栏 / 开始菜单 = 深色",     1, TK_BOOL },
    { L"AppsDark",              L"Theme",            L"应用窗口 = 深色",            0, TK_BOOL },
    { L"FixedWallpaper",        L"Theme",            L"固定壁纸（关闭聚焦轮播）",      1, TK_BOOL },

    { L"DisableNotificationCenter", L"Notify",       L"关闭通知中心",               1, TK_BOOL },
    { L"DisableToasts",         L"Notify",           L"关闭弹窗通知",               1, TK_BOOL },
    { L"DisableQuietHours",     L"Notify",           L"关闭专注助手（免打扰）",       1, TK_BOOL },
    { L"DisableConsumerFeatures", L"Notify",         L"关闭消费级建议 / 锁屏广告",     1, TK_BOOL },

    { L"ShowRecentlyAddedApps", L"StartRecommended", L"显示最近添加的应用",          0, TK_BOOL },
    { L"ShowRecommendedFiles",  L"StartRecommended", L"显示推荐的文件 / 最新文件",     0, TK_BOOL },
    { L"ShowRecommendationTips", L"StartRecommended",L"显示提示 / 快捷方式 / 新应用建议", 0, TK_BOOL },

    { L"FolderSettings",        L"StartFolders",     L"文件夹：设置",               1, TK_BOOL },
    { L"FolderFileExplorer",    L"StartFolders",     L"文件夹：文件资源管理器",        1, TK_BOOL },
    { L"FolderDownloads",       L"StartFolders",     L"文件夹：下载",               1, TK_BOOL },
    { L"FolderDocuments",       L"StartFolders",     L"文件夹：文档",               0, TK_BOOL },
    { L"FolderMusic",           L"StartFolders",     L"文件夹：音乐",               0, TK_BOOL },
    { L"FolderPictures",        L"StartFolders",     L"文件夹：图片",               0, TK_BOOL },
    { L"FolderVideos",          L"StartFolders",     L"文件夹：视频",               0, TK_BOOL },
    { L"FolderNetwork",         L"StartFolders",     L"文件夹：网络",               0, TK_BOOL },
    { L"FolderPersonal",        L"StartFolders",     L"文件夹：用户个人文件夹",        0, TK_BOOL },
};

/* ============================================================ 注册表值表（51 条） */
const RegOp g_ops[] = {
/*  { tweak,                       hive, type, flags, onVal,       offVal, subkey,   name,                                    sOn } */
    { T_TASKBAR_ALIGN,             CU, DW, W11,      0,           D,      K_ADV,    L"TaskbarAl",                            NULL },
    { T_TASKBAR_COMBINE,           CU, DW, 0,        0,           D,      K_ADV,    L"TaskbarGlomLevel",                     NULL },
    { T_TASKBAR_COMBINE,           CU, DW, 0,        0,           D,      K_ADV,    L"MMTaskbarGlomLevel",                   NULL },
    { T_HIDE_SEARCH,               CU, DW, 0,        1,           D,      K_SRCH,   L"SearchboxTaskbarModeCache",            NULL },
    { T_HIDE_SEARCH,               CU, DW, 0,        0,           D,      K_SRCH,   L"SearchboxTaskbarMode",                 NULL },
    { T_HIDE_TASKVIEW,             CU, DW, 0,        0,           D,      K_ADV,    L"ShowTaskViewButton",                   NULL },
    { T_HIDE_WIDGETS,              CU, DW, 0,        0,           D,      K_ADV,    L"TaskbarDa",                            NULL },
    { T_HIDE_CHAT,                 CU, DW, 0,        0,           D,      K_ADV,    L"TaskbarMn",                            NULL },
    { T_SHOW_SECONDS,              CU, DW, 0,        1,           D,      K_ADV,    L"ShowSecondsInSystemClock",             NULL },
    { T_HIDE_BELL,                 CU, DW, 0,        0,           D,      K_ADV,    L"ShowNotificationIcon",                 NULL },
    { T_EXPLORER_THISPC,           CU, DW, 0,        1,           D,      K_ADV,    L"LaunchTo",                             NULL },
    { T_WIN10_CTXMENU,             CU, SZ, W11,      0,           D,      K_CTX,    NULL,                                    L"" },

    { T_SHOW_THISPC,               CU, DW, 0,        0,           1,      K_NP,     C_THISPC,                                NULL },
    { T_SHOW_THISPC,               CU, DW, 0,        0,           1,      K_CP,     C_THISPC,                                NULL },
    { T_SHOW_RECYCLE,              CU, DW, 0,        0,           1,      K_NP,     C_RECYC,                                 NULL },
    { T_SHOW_RECYCLE,              CU, DW, 0,        0,           1,      K_CP,     C_RECYC,                                 NULL },
    { T_SHOW_CONTROLPANEL,         CU, DW, 0,        0,           1,      K_NP,     C_CPANEL,                                NULL },
    { T_SHOW_CONTROLPANEL,         CU, DW, 0,        0,           1,      K_CP,     C_CPANEL,                                NULL },
    { T_AUTO_ARRANGE,              CU, DW, 0,        1075839525,  D,      K_BAGS,   L"FFlags",                               NULL },
    { T_SORT_NAME,                 CU, SZ, 0,        0,           D,      K_ADV,    L"Sort",                                 L"Name" },
    { T_NO_ARROW,                  LM, SZ, ADM,      0,           D,      K_SICON,  L"29",                                   L"@IMAGERES" },
    { T_NO_SHIELD,                 LM, SZ, ADM,      0,           D,      K_SICON,  L"77",                                   L"@IMAGERES" },
    { T_NO_SUFFIX,                 CU, SZ, 0,        0,           D,      K_NAMING, L"ShortcutNameTemplate",                 L"%s.lnk" },

    { T_SYS_DARK,                  CU, DW, 0,        0,           1,      K_PERS,   L"SystemUsesLightTheme",                 NULL },
    { T_APPS_DARK,                 CU, DW, 0,        0,           1,      K_PERS,   L"AppsUseLightTheme",                    NULL },
    { T_FIXED_WALLPAPER,           CU, SZ, 0,        0,           D,      K_DSK,    L"Wallpaper",                            L"@WALLPAPER" },
    { T_FIXED_WALLPAPER,           CU, SZ, 0,        0,           D,      K_DSK,    L"WallpaperStyle",                       L"10" },
    { T_FIXED_WALLPAPER,           CU, SZ, 0,        0,           D,      K_DSK,    L"TileWallpaper",                        L"0" },

    { T_NO_NOTIF_CENTER,           CU, DW, 0,        1,           D,      K_EXPL,   L"DisableNotificationCenter",            NULL },
    { T_NO_NOTIF_CENTER,           CU, DW, 0,        0,           D,      K_PUSH,   L"ToastEnabled",                         NULL },
    { T_NO_TOASTS,                 CU, DW, 0,        0,           D,      K_NOTIF,  L"NOC_GLOBAL_SETTING_TOASTS_ENABLED",    NULL },
    { T_NO_TOASTS,                 CU, DW, 0,        1,           D,      K_PUSH,   L"NoCloudApplicationNotification",       NULL },
    { T_NO_QUIET_HOURS,            CU, DW, 0,        0,           D,      K_QUIET,  L"QuietHoursEnabled",                    NULL },

    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SubscribedContent-338387Enabled",      NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SubscribedContent-338388Enabled",      NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SubscribedContent-338389Enabled",      NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SubscribedContent-353694Enabled",      NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SubscribedContent-353696Enabled",      NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SubscribedContent-310093Enabled",      NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"RotatingLockScreenEnabled",            NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"RotatingLockScreenOverlayEnabled",     NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SoftLandingEnabled",                   NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SystemPaneSuggestionsEnabled",         NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_CDM,    L"SilentInstalledAppsEnabled",           NULL },
    { T_NO_CONSUMER,               CU, DW, 0,        0,           D,      K_UPE,    L"ScoobeSystemSettingEnabled",           NULL },

    { T_ST_RECENT_APPS,            CU, DW, 0,        1,           0,      K_START,  L"ShowRecentList",                       NULL },
    { T_ST_RECENT_APPS,            CU, DW, 0,        1,           0,      K_ADV,    L"Start_TrackProgs",                     NULL },
    { T_ST_RECENT_FILES,           CU, DW, 0,        1,           0,      K_ADV,    L"Start_TrackDocs",                      NULL },
    { T_ST_TIPS,                   CU, DW, 0,        1,           0,      K_ADV,    L"Start_IrisRecommendations",            NULL },
    { T_ST_TIPS,                   CU, DW, 0,        1,           0,      K_ADV,    L"Start_AccountNotifications",           NULL },

    { T_F_SETTINGS,                CU, BN, 0,        0,           D,      K_START,  L"VisiblePlaces",                        NULL },
};

const int g_opCount = (int)(sizeof(g_ops) / sizeof(g_ops[0]));

/* ============================================================ 值令牌展开 */
static const WCHAR *ExpandToken(const WCHAR *s, WCHAR *out, int cch)
{
    if (!s) return NULL;
    if (wcscmp(s, L"@IMAGERES") == 0) {
        wt_strlcpy(out, L"%systemroot%\\system32\\imageres.dll,197", (size_t)cch);
        return out;
    }
    if (wcscmp(s, L"@WALLPAPER") == 0) {
        WCHAR win[MAX_PATH];
        UINT n = GetWindowsDirectoryW(win, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) wt_strlcpy(win, L"C:\\Windows", MAX_PATH);
        wt_joinPath(out, cch, win, L"Web\\Wallpaper\\Windows\\img0.jpg");
        return out;
    }
    return s;
}

/* ============================================================ 三档 / 文件夹 */

int TweakCombineLevel(const IniFile *ini)
{
    const WCHAR *v = IniStr(ini, L"Shell", L"TaskbarCombine");
    if (!v) return 0;
    if (wt_ieq(v, L"始终") || wt_ieq(v, L"always")) return 0;
    if (wt_ieq(v, L"已满时") || wt_ieq(v, L"full"))   return 1;
    if (wt_ieq(v, L"从不") || wt_ieq(v, L"never"))    return 2;
    return -1;
}

/* 9 个文件夹 -> REG_BINARY。顺序与系统设置页面显示顺序一致。 */
static const struct { int tweak; const WCHAR *hex; } kPlaces[] = {
    { T_F_SETTINGS,  L"86087352AA5143429F7B2776584659D4" },
    { T_F_EXPLORER,  L"BC248A140CD68942A0806ED9BBA24882" },
    { T_F_DOCUMENTS, L"CED5342D5AFA434582F222E6EAF7773C" },
    { T_F_DOWNLOADS, L"2FB367E3DE895543BFCE61F37B18A937" },
    { T_F_PICTURES,  L"A0073F380AE8804CB05A86DB845DBC4D" },
    { T_F_MUSIC,     L"20060BB0517F324CAA1E34CC547F7315" },
    { T_F_VIDEOS,    L"C5A5B342867DF44280A493FACA7A88B5" },
    { T_F_NETWORK,   L"448175FE0D08AE428BDA34ED97B66394" },
    { T_F_PERSONAL,  L"4AB0BD744AF9684F8BD64398071DA8BC" },
};

WCHAR *TweakVisiblePlaces(const IniFile *ini, DWORD *outLen)
{
    BYTE *buf = (BYTE *)wt_alloc(sizeof(BYTE) * 16 * 9);
    DWORD n = 0;
    int i;
    for (i = 0; i < (int)(sizeof(kPlaces) / sizeof(kPlaces[0])); i++) {
        const TweakDef *td = &g_tweaks[kPlaces[i].tweak];
        if (IniBool(ini, td->section, td->id, td->defOn)) {
            int j;
            for (j = 0; j < 16; j++) {
                int hi = kPlaces[i].hex[j * 2], lo = kPlaces[i].hex[j * 2 + 1];
                int a = (hi <= L'9') ? hi - L'0' : (hi - L'A' + 10);
                int b = (lo <= L'9') ? lo - L'0' : (lo - L'A' + 10);
                buf[n++] = (BYTE)((a << 4) | b);
            }
        }
    }
    *outLen = n;
    return (WCHAR *)buf;   /* 返回的是字节缓冲，调用方按 BYTE* 用 */
}

/* ============================================================ 应用（按 ini） */

/* 计算某个 op 在本次应用时的目标 DWORD；返回 0=删除，1=写值和值本身 */
static int opTargetDword(const IniFile *ini, const RegOp *op, DWORD *out)
{
    int tw = op->tweak;
    int on;

    if (tw == T_TASKBAR_COMBINE) {
        int lv = TweakCombineLevel(ini);
        if (lv < 0) return -1;                       /* 档位名无法识别 -> 跳过 */
        *out = (DWORD)lv;
        return 1;
    }
    if (tw == T_AUTO_ARRANGE) {
        int aa = IniBool(ini, L"Desktop", L"AutoArrange", 1);
        int ag = IniBool(ini, L"Desktop", L"AlignToGrid", 1);
        if (!aa) return 0;                           /* 关 -> 删除 FFlags */
        *out = ag ? 1075839525u : 1075839521u;
        return 1;
    }

    {
        const TweakDef *td = &g_tweaks[tw];
        on = IniBool(ini, td->section, td->id, td->defOn);
    }
    if (on) { *out = op->onVal; return 1; }
    if (op->offVal == REGV_DEL) return 0;            /* 关 -> 删除 */
    *out = op->offVal;
    return 1;
}

void TweakApplyAll(const Ctx *ctx, const IniFile *ini, Report *rep)
{
    int i;
    int themeOn = IniBool(ini, L"Theme", L"ApplyTheme", 1);
    BYTE *vp = NULL;
    DWORD vpLen = 0;

    memset(rep, 0, sizeof(*rep));

    for (i = 0; i < g_opCount; i++) {
        const RegOp *op = &g_ops[i];
        const TweakDef *td = &g_tweaks[op->tweak];
        WCHAR sbuf[MAX_PATH * 2];
        const WCHAR *sv;
        int on;

        if (op->tweak == T_APPLY_THEME) continue;               /* 虚拟开关，无值 */
        if (!themeOn && (op->tweak == T_SYS_DARK || op->tweak == T_APPS_DARK ||
                         op->tweak == T_FIXED_WALLPAPER)) { rep->skipped++; continue; }
        if (op->tweak == T_ALIGN_GRID) continue;                /* 已并入 FFlags */

        if ((op->flags & OPF_WIN11) && !ctx->win11) {
            LogLine(L"  [跳过] %ls：Win10 不支持\n", td->label);
            rep->skipped++;
            continue;
        }
        if ((op->flags & OPF_ADMIN) && !ctx->admin) {
            LogLine(L"  [跳过] %ls：需要管理员权限\n", td->label);
            rep->skipped++;
            continue;
        }

        /* ---- 二进制：StartFolders ---- */
        if (op->type == REG_BINARY) {
            if (!vp) vp = (BYTE *)TweakVisiblePlaces(ini, &vpLen);
            if (ctx->dryRun) { rep->applied++; continue; }
            if (vpLen == 0) {
                if (RegDelValue(op->hive, op->subkey, op->name)) {
                    LogLine(L"  [写入] 开始菜单文件夹：全部分组关闭（删除 VisiblePlaces）\n");
                    rep->applied++;
                } else { LogLine(L"  [失败] VisiblePlaces 删除失败\n"); rep->failed++; }
            } else {
                if (RegWriteBin(op->hive, op->subkey, op->name, vp, vpLen)) {
                    LogLine(L"  [写入] 开始菜单文件夹（%lu 个）\n", vpLen / 16);
                    rep->applied++;
                } else { LogLine(L"  [失败] VisiblePlaces 写入失败\n"); rep->failed++; }
            }
            continue;
        }

        /* ---- DWORD ---- */
        if (op->type == REG_DWORD) {
            DWORD val = 0;
            int r = opTargetDword(ini, op, &val);
            if (r < 0) { LogLine(L"  [跳过] TaskbarCombine 档位名无法识别\n"); rep->skipped++; continue; }
            if (ctx->dryRun) { rep->applied++; continue; }
            if (r == 0) {
                if (RegDelValue(op->hive, op->subkey, op->name)) { rep->applied++; }
                else { LogLine(L"  [失败] 删除 %ls 失败\n", op->name); rep->failed++; }
            } else {
                if (RegWriteDword(op->hive, op->subkey, op->name, val)) { rep->applied++; }
                else { LogLine(L"  [失败] 写入 %ls 失败\n", op->name); rep->failed++; }
            }
            continue;
        }

        /* ---- SZ ---- */
        on = IniBool(ini, td->section, td->id, td->defOn);
        if (op->tweak == T_WIN10_CTXMENU) on = IniBool(ini, td->section, td->id, td->defOn);
        if (ctx->dryRun) { rep->applied++; continue; }

        if (!on) {
            if (RegDelValue(op->hive, op->subkey, op->name)) { rep->applied++; }
            else { LogLine(L"  [失败] 删除 %ls 失败\n", op->name ? op->name : L"(默认值)"); rep->failed++; }
            continue;
        }
        sv = ExpandToken(op->sOn, sbuf, MAX_PATH * 2);
        if (!sv) sv = L"";
        if (RegWriteSz(op->hive, op->subkey, op->name, sv)) { rep->applied++; }
        else { LogLine(L"  [失败] 写入 %ls 失败\n", op->name ? op->name : L"(默认值)"); rep->failed++; }
    }

    free(vp);
}

/* ============================================================ 应用（按快照） */

void TweakApplyStates(const Ctx *ctx, const Profile *pf, const DiffItem *diffs, int n, Report *rep)
{
    int i;
    memset(rep, 0, sizeof(*rep));
    for (i = 0; i < n; i++) {
        const RegOp *op = &g_ops[diffs[i].opIdx];
        const ValState *st = &pf->vals[diffs[i].opIdx];
        const TweakDef *td = &g_tweaks[op->tweak];

        if (diffs[i].kind == DIFF_SAME) { rep->unchanged++; continue; }
        if ((op->flags & OPF_ADMIN) && !ctx->admin) {
            LogLine(L"  [跳过] %ls：需要管理员权限\n", td->label);
            rep->skipped++;
            continue;
        }
        if (ctx->dryRun) { rep->applied++; continue; }

        if (!st->exists) {
            if (RegDelValue(op->hive, op->subkey, op->name)) { rep->applied++; }
            else { rep->failed++; LogLine(L"  [失败] 删除 %ls\\%ls\n", op->subkey, op->name ? op->name : L"(默认值)"); }
            continue;
        }
        if (st->type == REG_DWORD) {
            if (RegWriteDword(op->hive, op->subkey, op->name, st->dw)) rep->applied++;
            else { rep->failed++; LogLine(L"  [失败] 写入 %ls\n", op->name); }
        } else if (st->type == REG_BINARY) {
            if (RegWriteBin(op->hive, op->subkey, op->name, st->bin, st->binLen)) rep->applied++;
            else { rep->failed++; LogLine(L"  [失败] 写入 %ls\n", op->name); }
        } else {
            if (RegWriteSz(op->hive, op->subkey, op->name, st->sz ? st->sz : L"")) rep->applied++;
            else { rep->failed++; LogLine(L"  [失败] 写入 %ls\n", op->name); }
        }
    }
}

/* ============================================================ 快照 */

/* ★ 分配一个"所有值都为空"的 Profile —— 唯一正确的构造入口。
 *
 *   这里必须同时 memset(pf) 和 memset(pf->vals)。
 *   ProfileFree 会无条件 free 每一条的 sz / bin；这两个指针但凡带着
 *   未初始化的堆垃圾，free() 就会踩坏堆。
 *
 *   历史事故（务必保留这段注释）：ProfileLoadJson 曾经用 wt_alloc 拿到
 *   vals 后只写了 opIdx，没清零。而解析循环只处理配置文件里出现的项，
 *   exists==false 的项直接 continue、从不碰 sz/bin —— 于是这两个字段
 *   保留着堆里的陈旧值。结果是 --diff / --import 约 1/3 的运行以
 *   0xC0000374(堆损坏) 或 0xC0000005 崩溃，且崩溃点随机、表面症状
 *   是"进程卡 1~4 秒"（其实是 WER 收转储把进程挂住了）。
 *   任何新增的 Profile 构造路径都必须走这个函数。 */
Profile *ProfileNew(void)
{
    Profile *pf = (Profile *)wt_alloc(sizeof(Profile));
    memset(pf, 0, sizeof(*pf));
    pf->count = g_opCount;
    pf->vals = (ValState *)wt_alloc(sizeof(ValState) * (size_t)g_opCount);
    memset(pf->vals, 0, sizeof(ValState) * (size_t)g_opCount);
    return pf;
}

void ProfileFree(Profile *pf)
{
    int i;
    if (!pf) return;
    if (pf->vals) {
        for (i = 0; i < pf->count; i++) {
            free(pf->vals[i].sz);
            free(pf->vals[i].bin);
        }
        free(pf->vals);
    }
    free(pf->machine);
    free(pf->user);
    free(pf->os);
    free(pf->exported);
    free(pf);
    WtTrace("pf.exit");
}

Profile *TweakSnapshot(void)
{
    Profile *pf = ProfileNew();
    int i;
    WCHAR bufm[MAX_PATH], bufu[256];
    DWORD cch = MAX_PATH;

    for (i = 0; i < g_opCount; i++) {
        RegReadOp(&g_ops[i], &pf->vals[i]);
        pf->vals[i].opIdx = i;
    }

    bufm[0] = 0;
    if (!GetComputerNameW(bufm, &cch)) wt_strlcpy(bufm, L"(unknown)", MAX_PATH);
    pf->machine = wt_strdup(bufm);

    bufu[0] = 0;
    cch = 256;
    if (!GetUserNameW(bufu, &cch)) wt_strlcpy(bufu, L"(unknown)", 256);
    pf->user = wt_strdup(bufu);

    {
        WCHAR v[128];
        wt_winver(v, 128);
        pf->os = wt_strdup(v);
    }
    {
        WCHAR t[64];
        wt_nowIso(t, 64);
        pf->exported = wt_strdup(t);
    }
    return pf;
}

const RegOp *ProfileOpOf(const Profile *pf, int i)
{
    (void)pf;
    if (i < 0 || i >= g_opCount) return NULL;
    return &g_ops[i];
}

/* ============================================================ 从快照反推开关状态 */

/* 返回 1=开 0=关 -1=未知 */
int TweakReadBool(const Profile *pf, int tw)
{
    int i;
    if (tw == T_APPLY_THEME) return 1;

    if (tw == T_ALIGN_GRID) {
        for (i = 0; i < pf->count; i++)
            if (g_ops[i].tweak == T_AUTO_ARRANGE && pf->vals[i].exists)
                return (pf->vals[i].dw & 4u) ? 1 : 0;
        return 0;
    }
    if (tw == T_AUTO_ARRANGE) {
        for (i = 0; i < pf->count; i++)
            if (g_ops[i].tweak == T_AUTO_ARRANGE) {
                if (!pf->vals[i].exists) return 0;
                return (pf->vals[i].dw & 1u) ? 1 : 0;
            }
        return 0;
    }
    if (tw >= T_F_SETTINGS && tw <= T_F_PERSONAL) {
        int k;
        for (i = 0; i < pf->count; i++) {
            if (g_ops[i].type != REG_BINARY) continue;
            if (!pf->vals[i].exists || pf->vals[i].binLen < 16) return 0;
            for (k = 0; k < (int)(sizeof(kPlaces) / sizeof(kPlaces[0])); k++) {
                if (kPlaces[k].tweak != tw) continue;
                {
                    int j, hit = 1;
                    for (j = 0; j < 16; j++) {
                        int hi = kPlaces[k].hex[j * 2], lo = kPlaces[k].hex[j * 2 + 1];
                        int a = (hi <= L'9') ? hi - L'0' : (hi - L'A' + 10);
                        int b = (lo <= L'9') ? lo - L'0' : (lo - L'A' + 10);
                        if (pf->vals[i].bin[j] != (BYTE)((a << 4) | b)) { hit = 0; break; }
                    }
                    return hit;
                }
            }
            return 0;
        }
        return 0;
    }

    for (i = 0; i < pf->count; i++) {
        if (g_ops[i].tweak != tw) continue;
        if (g_ops[i].type == REG_DWORD) {
            if (!pf->vals[i].exists) return (g_ops[i].offVal == REGV_DEL) ? 0 : -1;
            if (pf->vals[i].dw == g_ops[i].onVal) return 1;
            if (g_ops[i].offVal != REGV_DEL && pf->vals[i].dw == g_ops[i].offVal) return 0;
            return -1;
        }
        if (g_ops[i].type == REG_SZ) {
            WCHAR exp[MAX_PATH * 2];
            const WCHAR *want;
            if (!pf->vals[i].exists) return 0;
            want = ExpandToken(g_ops[i].sOn, exp, MAX_PATH * 2);
            if (!want) return 0;
            return (pf->vals[i].sz && wt_ieq(pf->vals[i].sz, want)) ? 1 : -1;
        }
    }
    return -1;
}

/* TaskbarCombine：返回 0/1/2，-1 未知 */
int TweakReadCombine(const Profile *pf)
{
    int i;
    for (i = 0; i < pf->count; i++) {
        if (g_ops[i].tweak != T_TASKBAR_COMBINE) continue;
        if (!pf->vals[i].exists) return -1;
        if (pf->vals[i].dw > 2) return -1;
        return (int)pf->vals[i].dw;
    }
    return -1;
}
