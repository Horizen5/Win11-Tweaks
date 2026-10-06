/* ============================================================================
 *  wt.h  --  Win11 / Win10 「顺手设置」  公共头文件
 *  ----------------------------------------------------------------------------
 *  设计要点：
 *    · 全 Unicode（UTF-16），源文件用 /utf-8 编译，中文零乱码
 *    · 单 exe 双形态：无参数 -> GUI；带参数 -> 挂到父控制台走 CLI
 *    · 注册表直接走 Win32 API，不经 reg.exe，因此不触发 UCPD 拦截
 *      （UCPD 按“进程文件名”黑名单过滤，本程序名不在名单里）
 *    · 静态链接 CRT（/MT），产物零运行时依赖，拷到任何 Win10/11 都能跑
 * ========================================================================== */
#ifndef WT_H
#define WT_H

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ 常量 */
#define WTA_NAME   L"Win11 顺手设置"
#define WTA_VER    L"2.0.0"
#define WTA_EXE    L"Win11Tweaks.exe"

/* 注册表“关”动作：删除该值（还原系统默认） */
#define REGV_DEL   0xFFFFFFFFu

/* RegOp 标志 */
#define OPF_ADMIN  0x01     /* 需管理员（写 HKLM） */
#define OPF_WIN11  0x02     /* 仅 Win11 有意义 */

/* hive */
#define HIVE_CU    0
#define HIVE_LM    1

/* ------------------------------------------------------------------ 开关 */
enum {
    /* [Shell] 任务栏 / 右键菜单 —— 10 项 */
    T_TASKBAR_ALIGN = 0,
    T_TASKBAR_COMBINE,
    T_HIDE_SEARCH,
    T_HIDE_TASKVIEW,
    T_HIDE_WIDGETS,
    T_HIDE_CHAT,
    T_SHOW_SECONDS,
    T_HIDE_BELL,
    T_EXPLORER_THISPC,
    T_WIN10_CTXMENU,
    /* [Desktop] 桌面 —— 9 项 */
    T_SHOW_THISPC,
    T_SHOW_RECYCLE,
    T_SHOW_CONTROLPANEL,
    T_AUTO_ARRANGE,
    T_ALIGN_GRID,
    T_SORT_NAME,
    T_NO_ARROW,
    T_NO_SHIELD,
    T_NO_SUFFIX,
    /* [Theme] 主题 —— 4 项 */
    T_APPLY_THEME,
    T_SYS_DARK,
    T_APPS_DARK,
    T_FIXED_WALLPAPER,
    /* [Notify] 通知 —— 4 项 */
    T_NO_NOTIF_CENTER,
    T_NO_TOASTS,
    T_NO_QUIET_HOURS,
    T_NO_CONSUMER,
    /* [StartRecommended] 开始菜单“推荐” —— 3 项 */
    T_ST_RECENT_APPS,
    T_ST_RECENT_FILES,
    T_ST_TIPS,
    /* [StartFolders] 开始菜单文件夹 —— 9 项 */
    T_F_SETTINGS,
    T_F_EXPLORER,
    T_F_DOWNLOADS,
    T_F_DOCUMENTS,
    T_F_MUSIC,
    T_F_PICTURES,
    T_F_VIDEOS,
    T_F_NETWORK,
    T_F_PERSONAL,
    T_COUNT                     /* = 39 */
};

/* 开关种类 */
enum { TK_BOOL = 0, TK_ENUM = 1, TK_VIRTUAL = 2 };

typedef struct {
    const WCHAR *id;        /* settings.ini 里的键名 */
    const WCHAR *section;   /* settings.ini 段名 */
    const WCHAR *label;     /* 界面显示名 */
    int          defOn;     /* 默认值（1 开 / 0 关） */
    int          kind;
} TweakDef;

/* --------------------------------------------------------------- 注册表项 */
typedef struct {
    BYTE  tweak;            /* 归属开关（TweakId） */
    BYTE  hive;             /* HIVE_CU / HIVE_LM */
    BYTE  type;             /* REG_DWORD / REG_SZ / REG_BINARY */
    BYTE  flags;            /* OPF_* */
    DWORD onVal;            /* DWORD：开关=1 时写入 */
    DWORD offVal;           /* DWORD：开关=0 时写入；REGV_DEL=删除 */
    const WCHAR *subkey;
    const WCHAR *name;      /* NULL = 键的默认值 */
    const WCHAR *sOn;       /* REG_SZ：开关=1 时写入的字符串 */
} RegOp;

/* 一次快照里某个注册表值的状态 */
typedef struct {
    int    opIdx;
    BOOL   exists;          /* 该值在本机是否存在（关键：区分“不存在”与“=0”） */
    DWORD  type;            /* 实际类型 */
    DWORD  dw;
    WCHAR *sz;
    BYTE  *bin;
    DWORD  binLen;
} ValState;

typedef struct {
    WCHAR    *machine;
    WCHAR    *user;
    WCHAR    *os;
    WCHAR    *exported;
    int       count;
    ValState *vals;
} Profile;

/* 差异种类 */
enum { DIFF_SAME = 0, DIFF_ADD, DIFF_CHG, DIFF_DEL, DIFF_NA };

typedef struct { int opIdx; int kind; } DiffItem;

typedef struct { int applied, skipped, failed, unchanged; } Report;

typedef struct {
    BOOL   win11;
    BOOL   admin;
    BOOL   dryRun;
    WCHAR  exeDir[MAX_PATH];
} Ctx;

/* ------------------------------------------------------------ 动态字符串 */
typedef struct { WCHAR *p; size_t len, cap; } StrBuf;

void  sbInit(StrBuf *sb);
void  sbFree(StrBuf *sb);
void  sbClear(StrBuf *sb);
void  sbAdd(StrBuf *sb, const WCHAR *s);
void  sbAddN(StrBuf *sb, const WCHAR *s, size_t n);
void  sbAddCh(StrBuf *sb, WCHAR c);
void  sbAddF(StrBuf *sb, const WCHAR *fmt, ...);
void  sbAddA(StrBuf *sb, const char *s);        /* 追加 UTF-8 字节串 */
WCHAR *sbDetach(StrBuf *sb);

/* 日志：CLI 模式直接进控制台，GUI 模式先进缓冲区再由窗口刷出 */
extern int    g_guiMode;
extern StrBuf g_logBuf;

/* ------------------------------------------------------------------ 工具 */
void  *wt_alloc(size_t n);
void  *wt_realloc(void *p, size_t n);
WCHAR *wt_strdup(const WCHAR *s);
WCHAR *wt_strndup(const WCHAR *s, size_t n);
int    wt_ieq(const WCHAR *a, const WCHAR *b);
int    wt_ieqn(const WCHAR *a, const WCHAR *b, size_t n);
WCHAR *wt_trim(WCHAR *s);
size_t wt_utf8_to_utf16(const char *in, int inLen, WCHAR **out);
char  *wt_utf16_to_utf8(const WCHAR *in, int inLen, int *outLen);
char  *wt_utf16_to_cp(const WCHAR *in, int inLen, UINT cp, int *outLen);
int    wt_atoi(const WCHAR *s);
void   wt_hexenc(const BYTE *d, DWORD n, StrBuf *out);
int    wt_hexdec(const WCHAR *h, BYTE **out, DWORD *outLen);
void   wt_nowIso(WCHAR *buf, int cch);
void   wt_nowStamp(WCHAR *buf, int cch);        /* YYYYMMDD_HHMMSS */
void   wt_winver(WCHAR *buf, int cch);          /* "Windows 11 Build 26300" */
void   wt_getExeDir(WCHAR *buf, int cch);
BOOL   wt_mkdirTree(const WCHAR *path);
BOOL   wt_fileExists(const WCHAR *path);
BOOL   wt_readAll(const WCHAR *path, BYTE **out, DWORD *outLen);
BOOL   wt_writeAll(const WCHAR *path, const BYTE *d, DWORD n);
void   wt_joinPath(WCHAR *out, int cch, const WCHAR *a, const WCHAR *b);
void   wt_strlcpy(WCHAR *dst, const WCHAR *src, size_t cch);
void   wt_strlcat(WCHAR *dst, const WCHAR *src, size_t cch);
int    wt_pathEndsWithI(const WCHAR *path, const WCHAR *name);

/* 性能埋点：仅在 -DWT_ENABLE_TRACE 的排错构建里存在（正式版编译期直接消失，
   连一次函数调用都没有）。开启后需设环境变量 WT_TRACE=1 才会写日志。 */
#ifdef WT_ENABLE_TRACE
void   WtTrace(const char *tag);
#else
#define WtTrace(tag) ((void)0)
#endif

/* ------------------------------------------------------------ 控制台输出 */
void   ConInit(void);
void   ConWrite(const WCHAR *s);
void   ConWriteF(const WCHAR *fmt, ...);
void   ConCol(int color);                        /* 设置颜色，-1 恢复默认 */
void   ConPause(void);                           /* 交互式才等按键 */
int    ConIsFresh(void);                         /* 1 = 我们新建的控制台窗口（双击场景） */
int    ConYes(const WCHAR *prompt);              /* 询问 y/n；无控制台则默认同意 */

/* -------------------------------------------------------------- 权限 */
BOOL   wt_isElevated(void);
BOOL   wt_relaunchAsAdmin(const WCHAR *args);
BOOL   wt_isWin11(void);

/* ------------------------------------------------------------------ ini */
typedef struct { WCHAR *sec, *key, *val; } IniPair;
typedef struct { IniPair *p; int n; } IniFile;

BOOL   IniLoad(const WCHAR *path, IniFile *f);   /* 不会失败：文件不存在则 n=0 */
void   IniFree(IniFile *f);
void   IniAdd(IniFile *f, const WCHAR *sec, const WCHAR *key, const WCHAR *val);
int    IniBool(const IniFile *f, const WCHAR *sec, const WCHAR *key, int def);
const WCHAR *IniStr(const IniFile *f, const WCHAR *sec, const WCHAR *key);

/* -------------------------------------------------------------- 注册表 */
BOOL  RegReadOp(const RegOp *op, ValState *st);          /* 读某个受管值 */
BOOL  RegWriteDword(BYTE hive, const WCHAR *sub, const WCHAR *name, DWORD v);
BOOL  RegWriteSz(BYTE hive, const WCHAR *sub, const WCHAR *name, const WCHAR *v);
BOOL  RegWriteBin(BYTE hive, const WCHAR *sub, const WCHAR *name, const BYTE *d, DWORD n);
BOOL  RegDelValue(BYTE hive, const WCHAR *sub, const WCHAR *name);
BOOL  RegWriteVal(BYTE hive, const WCHAR *sub, const WCHAR *name, DWORD type, const void *d, DWORD n);

/* -------------------------------------------------------------- 开关表 */
extern const TweakDef g_tweaks[T_COUNT];
extern const RegOp    g_ops[];
extern const int      g_opCount;

/* 应用：按 ini 把全部开关刷成目标状态 */
void  TweakApplyAll(const Ctx *ctx, const IniFile *ini, Report *rep);
/* 应用：直接按一份“目标状态数组”写（用于导入恢复） */
void  TweakApplyStates(const Ctx *ctx, const Profile *pf, const DiffItem *diffs, int n, Report *rep);
/* 读取当前 51 个受管值的真实状态 */
Profile *TweakSnapshot(void);
/* 统一构造 Profile：同时清零 pf 与 pf->vals。
   必须用它，不要手工 wt_alloc —— 详见 tweaks.c 里的历史事故注释。 */
Profile *ProfileNew(void);
void     ProfileFree(Profile *pf);

/* 特殊项辅助 */
int   TweakCombineLevel(const IniFile *ini);      /* 返回 0/1/2，或 -1 表示无法识别 */
WCHAR *TweakVisiblePlaces(const IniFile *ini, DWORD *outLen); /* 返回 REG_BINARY 字节 */
int   TweakReadBool(const Profile *pf, int tw);   /* 从快照反推开关：1/0/-1 */
int   TweakReadCombine(const Profile *pf);        /* 0/1/2，-1 未知 */

/* -------------------------------------------------------------- profile */
Profile *ProfileFromStates(ValState *vals, int n);
BOOL     ProfileSaveJson(const Profile *pf, const WCHAR *path);
Profile *ProfileLoadJson(const WCHAR *path, WCHAR *err, int errCch);
int      ProfileDiff(const Profile *pf, const Profile *cur, DiffItem **out);
const RegOp *ProfileOpOf(const Profile *pf, int i);
void     ValToText(const ValState *st, WCHAR *buf, int cch);
BOOL     MakeBackupProxy(const Ctx *ctx, const Profile *cur, WCHAR *outPath, int cch);

/* --------------------------------------------------------------- 界面 */
int  CliMain(const Ctx *ctx, int argc, WCHAR **argv);
int  GuiMain(const Ctx *ctx);
void LogLine(const WCHAR *fmt, ...);              /* GUI/CLI 通用日志回调 */

#endif /* WT_H */
