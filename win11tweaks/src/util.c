/* ============================================================================
 *  util.c  --  通用工具层
 *  内存 / 字符串 / UTF-8<->UTF-16 / 文件 / 路径 / 时间 / 控制台 / 提权 / ini
 * ========================================================================== */
#include "wt.h"

/* ============================================================ 全局日志 */

int    g_guiMode = 0;          /* 1 = GUI 模式，日志进缓冲区 */
StrBuf g_logBuf;

/* ============================================================ 内存 */

void *wt_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) ExitProcess(2);
    return p;
}

void *wt_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) ExitProcess(2);
    return q;
}

WCHAR *wt_strdup(const WCHAR *s)
{
    size_t n;
    WCHAR *d;
    if (!s) return NULL;
    n = wcslen(s) + 1;
    d = (WCHAR *)wt_alloc(n * sizeof(WCHAR));
    memcpy(d, s, n * sizeof(WCHAR));
    return d;
}

WCHAR *wt_strndup(const WCHAR *s, size_t n)
{
    WCHAR *d = (WCHAR *)wt_alloc((n + 1) * sizeof(WCHAR));
    memcpy(d, s, n * sizeof(WCHAR));
    d[n] = 0;
    return d;
}

/* ============================================================ StrBuf */

void sbInit(StrBuf *sb) { sb->p = NULL; sb->len = 0; sb->cap = 0; }

void sbFree(StrBuf *sb) { free(sb->p); sb->p = NULL; sb->len = sb->cap = 0; }

void sbClear(StrBuf *sb) { sb->len = 0; if (sb->p) sb->p[0] = 0; }

static void sbGrow(StrBuf *sb, size_t need)
{
    size_t cap;
    if (sb->len + need + 1 <= sb->cap) return;
    cap = sb->cap ? sb->cap : 64;
    while (cap < sb->len + need + 1) cap *= 2;
    sb->p = (WCHAR *)wt_realloc(sb->p, cap * sizeof(WCHAR));
    sb->cap = cap;
    if (sb->len == 0) sb->p[0] = 0;
}

void sbAddN(StrBuf *sb, const WCHAR *s, size_t n)
{
    if (!s || !n) return;
    sbGrow(sb, n);
    memcpy(sb->p + sb->len, s, n * sizeof(WCHAR));
    sb->len += n;
    sb->p[sb->len] = 0;
}

void sbAdd(StrBuf *sb, const WCHAR *s)
{
    if (s) sbAddN(sb, s, wcslen(s));
}

void sbAddCh(StrBuf *sb, WCHAR c)
{
    sbGrow(sb, 1);
    sb->p[sb->len++] = c;
    sb->p[sb->len] = 0;
}

void sbAddF(StrBuf *sb, const WCHAR *fmt, ...)
{
    WCHAR buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 2048, _TRUNCATE, fmt, ap);
    va_end(ap);
    sbAdd(sb, buf);
}

/* 追加一段 UTF-8 字节串（内部转 UTF-16） */
void sbAddA(StrBuf *sb, const char *s)
{
    WCHAR *w;
    size_t n;
    if (!s) return;
    n = wt_utf8_to_utf16(s, (int)strlen(s), &w);
    sbAddN(sb, w, n);
    free(w);
}

WCHAR *sbDetach(StrBuf *sb)
{
    WCHAR *p = sb->p;
    if (!p) { p = (WCHAR *)wt_alloc(sizeof(WCHAR)); p[0] = 0; }
    sb->p = NULL; sb->len = sb->cap = 0;
    return p;
}

/* ============================================================ 字符串 */

int wt_ieq(const WCHAR *a, const WCHAR *b) { return a && b && _wcsicmp(a, b) == 0; }

int wt_ieqn(const WCHAR *a, const WCHAR *b, size_t n)
{
    return a && b && _wcsnicmp(a, b, n) == 0;
}

/* 去首尾空白（原地，返回起始指针） */
WCHAR *wt_trim(WCHAR *s)
{
    WCHAR *e;
    if (!s) return NULL;
    while (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n') s++;
    e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n')) e--;
    *e = 0;
    return s;
}

int wt_atoi(const WCHAR *s)
{
    int v = 0, neg = 0;
    if (!s) return 0;
    while (*s == L' ') s++;
    if (*s == L'-') { neg = 1; s++; }
    while (*s >= L'0' && *s <= L'9') v = v * 10 + (*s++ - L'0');
    return neg ? -v : v;
}

void wt_strlcpy(WCHAR *dst, const WCHAR *src, size_t cch)
{
    if (!cch) return;
    while (cch > 1 && src && *src) { *dst++ = *src++; cch--; }
    *dst = 0;
}

void wt_strlcat(WCHAR *dst, const WCHAR *src, size_t cch)
{
    size_t n = wcslen(dst);
    if (n >= cch) return;
    wt_strlcpy(dst + n, src, cch - n);
}

void wt_joinPath(WCHAR *out, int cch, const WCHAR *a, const WCHAR *b)
{
    size_t n;
    wt_strlcpy(out, a, (size_t)cch);
    n = wcslen(out);
    if (n && out[n - 1] != L'\\' && out[n - 1] != L'/') wt_strlcat(out, L"\\", (size_t)cch);
    wt_strlcat(out, b, (size_t)cch);
}

int wt_pathEndsWithI(const WCHAR *path, const WCHAR *name)
{
    size_t lp, ln;
    if (!path || !name) return 0;
    lp = wcslen(path); ln = wcslen(name);
    if (ln > lp) return 0;
    return _wcsicmp(path + lp - ln, name) == 0;
}

/* ============================================================ 编码 */

size_t wt_utf8_to_utf16(const char *in, int inLen, WCHAR **out)
{
    int n;
    WCHAR *w;
    if (!in) { *out = NULL; return 0; }
    if (inLen < 0) inLen = (int)strlen(in);
    if (inLen == 0) { w = (WCHAR *)wt_alloc(sizeof(WCHAR)); w[0] = 0; *out = w; return 0; }
    /* 跳过 UTF-8 BOM */
    if (inLen >= 3 && (unsigned char)in[0] == 0xEF &&
        (unsigned char)in[1] == 0xBB && (unsigned char)in[2] == 0xBF) { in += 3; inLen -= 3; }
    n = MultiByteToWideChar(CP_UTF8, 0, in, inLen, NULL, 0);
    if (n <= 0) {
        /* 非法 UTF-8 -> 按系统 ANSI(GBK) 再试 */
        n = MultiByteToWideChar(CP_ACP, 0, in, inLen, NULL, 0);
        if (n <= 0) { w = (WCHAR *)wt_alloc(sizeof(WCHAR)); w[0] = 0; *out = w; return 0; }
        w = (WCHAR *)wt_alloc((n + 1) * sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, in, inLen, w, n);
    } else {
        w = (WCHAR *)wt_alloc((n + 1) * sizeof(WCHAR));
        MultiByteToWideChar(CP_UTF8, 0, in, inLen, w, n);
    }
    w[n] = 0;
    *out = w;
    return (size_t)n;
}

char *wt_utf16_to_utf8(const WCHAR *in, int inLen, int *outLen)
{
    int n;
    char *a;
    if (!in) { if (outLen) *outLen = 0; return NULL; }
    if (inLen < 0) inLen = (int)wcslen(in);
    n = WideCharToMultiByte(CP_UTF8, 0, in, inLen, NULL, 0, NULL, NULL);
    a = (char *)wt_alloc((size_t)n + 1);
    WideCharToMultiByte(CP_UTF8, 0, in, inLen, a, n, NULL, NULL);
    a[n] = 0;
    if (outLen) *outLen = n;
    return a;
}

/* 转成任意代码页。cp 传 CP_UTF8 就是上面的行为。
   命令行输出给"管道"时必须用系统 OEM 代码页：cmd 和 PowerShell 都是
   按它解码子进程输出的，发 UTF-8 会在中文系统上变成乱码。 */
char *wt_utf16_to_cp(const WCHAR *in, int inLen, UINT cp, int *outLen)
{
    int n;
    char *a;
    if (!in) { if (outLen) *outLen = 0; return NULL; }
    if (inLen < 0) inLen = (int)wcslen(in);
    if (cp == CP_UTF8) return wt_utf16_to_utf8(in, inLen, outLen);
    n = WideCharToMultiByte(cp, 0, in, inLen, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    a = (char *)wt_alloc((size_t)n + 1);
    WideCharToMultiByte(cp, 0, in, inLen, a, n, NULL, NULL);
    a[n] = 0;
    if (outLen) *outLen = n;
    return a;
}

void wt_hexenc(const BYTE *d, DWORD n, StrBuf *out)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    DWORD i;
    for (i = 0; i < n; i++) {
        sbAddCh(out, hex[(d[i] >> 4) & 0xF]);
        sbAddCh(out, hex[d[i] & 0xF]);
    }
}

static int hexVal(WCHAR c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

int wt_hexdec(const WCHAR *h, BYTE **out, DWORD *outLen)
{
    size_t n, i;
    BYTE *b;
    if (!h) return 0;
    n = wcslen(h);
    if (n % 2) return 0;
    b = (BYTE *)wt_alloc(n / 2 + 1);
    for (i = 0; i < n; i += 2) {
        int hi = hexVal(h[i]), lo = hexVal(h[i + 1]);
        if (hi < 0 || lo < 0) { free(b); return 0; }
        b[i / 2] = (BYTE)((hi << 4) | lo);
    }
    *out = b;
    *outLen = (DWORD)(n / 2);
    return 1;
}

/* ============================================================ 性能埋点 */

#ifdef WT_ENABLE_TRACE
/* WT_TRACE=1 时，把 "QPC计数 进程ID 标记" 追加到 %TEMP%\wt_trace.log。
   刻意用原始 Win32 调用（不经 CRT），避免埋点本身引入额外开销。
   时间戳在写日志之前取，这样"写日志"本身的耗时不会被算进上一段。
   QPC 频率全系统一致，父进程用 Stopwatch::Frequency 换算即可。
   第一次调用做一次环境变量判定，之后走静态标志位，关闭时近乎零开销。 */
void WtTrace(const char *tag)
{
    static int on = -1;
    WCHAR p[MAX_PATH];
    HANDLE h;
    char buf[256];
    DWORD wr;
    int n;
    LARGE_INTEGER c;

    if (on < 0) {
        WCHAR v[8];
        on = (GetEnvironmentVariableW(L"WT_TRACE", v, 8) != 0) ? 1 : 0;
    }
    if (!on) return;

    QueryPerformanceCounter(&c);

    GetTempPathW(MAX_PATH, p);
    wt_strlcat(p, L"wt_trace.log", MAX_PATH);
    h = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%llu %lu %s\n",
                    (unsigned long long)c.QuadPart,
                    (unsigned long)GetCurrentProcessId(), tag);
    if (n > 0) WriteFile(h, buf, (DWORD)n, &wr, NULL);
    CloseHandle(h);
}
#endif /* WT_ENABLE_TRACE */

/* ============================================================ 文件 */

BOOL wt_fileExists(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
}

BOOL wt_readAll(const WCHAR *path, BYTE **out, DWORD *outLen)
{
    HANDLE h;
    DWORD sz, rd = 0;
    BYTE *b;
    WtTrace("read.enter");
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    WtTrace("read.opened");
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    sz = GetFileSize(h, NULL);
    WtTrace("read.sized");
    if (sz == INVALID_FILE_SIZE) { CloseHandle(h); return FALSE; }
    b = (BYTE *)wt_alloc((size_t)sz + 2);
    if (sz && !ReadFile(h, b, sz, &rd, NULL)) { free(b); CloseHandle(h); return FALSE; }
    WtTrace("read.read");
    b[rd] = 0; b[rd + 1] = 0;
    CloseHandle(h);
    WtTrace("read.closed");
    *out = b;
    *outLen = rd;
    return TRUE;
}

BOOL wt_writeAll(const WCHAR *path, const BYTE *d, DWORD n)
{
    HANDLE h;
    DWORD wr = 0;
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (n && !WriteFile(h, d, n, &wr, NULL)) { CloseHandle(h); return FALSE; }
    CloseHandle(h);
    return TRUE;
}

BOOL wt_mkdirTree(const WCHAR *path)
{
    WCHAR tmp[MAX_PATH * 2];
    size_t i, n;
    wt_strlcpy(tmp, path, MAX_PATH * 2);
    n = wcslen(tmp);
    for (i = 3; i < n; i++) {
        if (tmp[i] == L'\\' || tmp[i] == L'/') {
            WCHAR c = tmp[i];
            tmp[i] = 0;
            CreateDirectoryW(tmp, NULL);
            tmp[i] = c;
        }
    }
    CreateDirectoryW(tmp, NULL);
    return TRUE;
}

void wt_getExeDir(WCHAR *buf, int cch)
{
    WCHAR *p;
    GetModuleFileNameW(NULL, buf, (DWORD)cch);
    p = wcsrchr(buf, L'\\');
    if (p) *p = 0;
}

/* ============================================================ 时间 */

void wt_nowIso(WCHAR *buf, int cch)
{
    SYSTEMTIME st;
    TIME_ZONE_INFORMATION tz;
    LONG bias = 0;
    WCHAR sign = L'+';
    GetLocalTime(&st);
    if (GetTimeZoneInformation(&tz) != TIME_ZONE_ID_INVALID) bias = tz.Bias;
    /* 注意：这里只用 Bias 近似；对配置文件用途足够 */
    _snwprintf_s(buf, (size_t)cch, _TRUNCATE,
                 L"%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 sign, 8, 0);
}

void wt_nowStamp(WCHAR *buf, int cch)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf_s(buf, (size_t)cch, _TRUNCATE, L"%04d%02d%02d_%02d%02d%02d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

/* 系统版本（用 RtlGetVersion，GetVersionEx 会撒谎） */
typedef struct {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} WT_OSVIW;

static BOOL wt_osver(WT_OSVIW *vi)
{
    typedef LONG (WINAPI *PFN)(WT_OSVIW *);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    PFN fn;
    if (!nt) return FALSE;
    fn = (PFN)(void *)GetProcAddress(nt, "RtlGetVersion");
    if (!fn) return FALSE;
    memset(vi, 0, sizeof(*vi));
    vi->dwOSVersionInfoSize = sizeof(*vi);
    return fn(vi) == 0;
}

BOOL wt_isWin11(void)
{
    WT_OSVIW vi;
    if (!wt_osver(&vi)) return FALSE;
    return (vi.dwMajorVersion >= 10 && vi.dwBuildNumber >= 22000);
}

void wt_winver(WCHAR *buf, int cch)
{
    WT_OSVIW vi;
    if (!wt_osver(&vi)) { wt_strlcpy(buf, L"Windows (unknown)", (size_t)cch); return; }
    if (vi.dwMajorVersion >= 10 && vi.dwBuildNumber >= 22000)
        _snwprintf_s(buf, (size_t)cch, _TRUNCATE, L"Windows 11 Build %lu", vi.dwBuildNumber);
    else if (vi.dwMajorVersion == 10)
        _snwprintf_s(buf, (size_t)cch, _TRUNCATE, L"Windows 10 Build %lu", vi.dwBuildNumber);
    else
        _snwprintf_s(buf, (size_t)cch, _TRUNCATE, L"Windows %lu.%lu Build %lu",
                     vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
}

/* ============================================================ 控制台 */

static HANDLE g_out = INVALID_HANDLE_VALUE;
static HANDLE g_in  = INVALID_HANDLE_VALUE;
static int    g_hasConsole = 0;
static int    g_fresh = 0;
static int    g_redirected = 0;
static int    g_outDisk = 0;      /* 1 = 输出目标是真实文件（可以用 UTF-8） */
static int    g_wroteAny = 0;
static WORD   g_defAttr = 7;

void ConInit(void)
{
    DWORD m;
    HANDLE h0, hOrigOut = NULL;

    /* ★ 必须在 AttachConsole/AllocConsole 之前判断，并且把句柄本身也留住：
       AllocConsole 会把标准句柄重置成新控制台，之后就再也看不出
       "调用方其实把输出重定向到了管道/文件"了 —— 连句柄本身都会丢。
       历史事故：曾经只记了 g_redirected 标志、g_out 却在 AllocConsole 之后才取。
       结果父进程没有控制台时，`--status > out.txt` 的输出会全部写进一个
       看不见的新控制台窗口，文件留空。 */
    h0 = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h0 && h0 != INVALID_HANDLE_VALUE) {
        DWORD ft = GetFileType(h0);
        if (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK) {
            g_redirected = 1;
            g_outDisk = (ft == FILE_TYPE_DISK);
            hOrigOut = h0;          /* 把重定向目标原样保留下来 */
        }
    }

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        /* 输出已经重定向到文件/管道时，别再多造一个控制台窗口 */
        if (!g_redirected) { if (AllocConsole()) g_fresh = 1; }
    }
    /* 故意不调用 SetConsoleOutputCP/SetConsoleCP：
       我们输出走 WriteConsoleW（Unicode 原生，与代码页无关），
       输入走 ReadConsoleInputW，都不需要改代码页。
       改了反而会污染用户这个控制台窗口的状态，影响他后面的命令。 */

    if (g_redirected && hOrigOut) {
        /* 重定向优先：绝不能改用新控制台的句柄 */
        g_out = hOrigOut;
        g_hasConsole = 0;
    } else {
        g_out = GetStdHandle(STD_OUTPUT_HANDLE);
        g_hasConsole = (g_out && g_out != INVALID_HANDLE_VALUE && GetConsoleMode(g_out, &m));
        /* 只有在“完全没有标准输出句柄”时才去开 CONOUT$。 */
        if (!g_hasConsole && (!g_out || g_out == INVALID_HANDLE_VALUE)) {
            HANDLE h = CreateFileW(L"CONOUT$", GENERIC_WRITE | GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                g_out = h;
                g_hasConsole = GetConsoleMode(h, &m) ? 1 : 0;
            }
        }
    }
    if (g_hasConsole) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(g_out, &csbi)) g_defAttr = csbi.wAttributes;
    }
    g_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!g_in || g_in == INVALID_HANDLE_VALUE)
        g_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
}

void ConWrite(const WCHAR *s)
{
    DWORD n, wr;
    if (!s || !*s) return;
    if (g_out == INVALID_HANDLE_VALUE) return;
    n = (DWORD)wcslen(s);
    if (g_hasConsole) {
        /* 控制台自己会把 \n 当换行处理；WriteConsoleW 与代码页无关 */
        WriteConsoleW(g_out, s, n, &wr, NULL);
    } else {
        /* 重定向到文件/管道：必须把裸 \n 补成 \r\n，否则在 Windows 上会串行 */
        StrBuf t;
        const WCHAR *p;
        int len = 0;
        char *a;
        UINT cp;
        sbInit(&t);
        for (p = s; *p; p++) {
            if (*p == L'\n' && (p == s || p[-1] != L'\r')) sbAdd(&t, L"\r\n");
            else sbAddCh(&t, *p);
        }

        /* 输出编码按目标分流：
             · 真实文件 -> UTF-8（外加 BOM，记事本/Excel/VSCode 都能正确识别）
             · 管道     -> 系统 OEM 代码页。cmd 和 PowerShell 就是按它解码
                           子进程输出的；发 UTF-8 会在中文系统上变成乱码，
                           而且 PowerShell 的 `>` 还会把乱码再转成 UTF-16 落盘。 */
        cp = g_outDisk ? CP_UTF8 : GetOEMCP();
        a = wt_utf16_to_cp(t.p, (int)t.len, cp, &len);
        if (a) {
            if (g_outDisk && !g_wroteAny) {
                static const BYTE bom[3] = { 0xEF, 0xBB, 0xBF };
                WriteFile(g_out, bom, 3, &wr, NULL);
            }
            WriteFile(g_out, a, (DWORD)len, &wr, NULL);
            g_wroteAny = 1;
            free(a);
        }
        sbFree(&t);
    }
}

void ConWriteF(const WCHAR *fmt, ...)
{
    WCHAR buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 4096, _TRUNCATE, fmt, ap);
    va_end(ap);
    ConWrite(buf);
}

void ConCol(int color)
{
    if (g_hasConsole && g_out != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(g_out, color < 0 ? g_defAttr : (WORD)color);
}

void ConPause(void)
{
    if (!g_hasConsole) return;
    ConWrite(L"\n按任意键退出...");
    if (g_in && g_in != INVALID_HANDLE_VALUE) FlushConsoleInputBuffer(g_in);
    {
        DWORD m;
        if (g_in && g_in != INVALID_HANDLE_VALUE && GetConsoleMode(g_in, &m)) {
            INPUT_RECORD ir;
            DWORD rd;
            for (;;) {
                if (!ReadConsoleInputW(g_in, &ir, 1, &rd)) break;
                if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) break;
            }
        }
    }
}

/* 只有在“确实是双击打开、并且控制台上只有我们自己”时才等按键。
   判据（三个同时成立）：
     1) 输出没有被重定向（启动时句柄不是管道/磁盘文件）
     2) 输出句柄确实是一个控制台
     3) 控制台上只挂着本进程 → 说明这个控制台是我们 AllocConsole 新建的，
        没有外层 shell 在等着，此时弹窗式等待才是安全的
   否则一律不等，避免脚本/重定向场景永久挂起。 */
int ConIsFresh(void)
{
    DWORD pids[8];
    DWORD n;
    if (g_redirected) return 0;
    if (!g_hasConsole) return 0;
    n = GetConsoleProcessList(pids, 8);
    return (n <= 1) ? 1 : 0;
}

/* 询问 y/n。没有可用控制台输入时默认返回“同意”，避免无人值守场景卡死。 */
int ConYes(const WCHAR *prompt)
{
    WCHAR buf[32];
    DWORD rd = 0;
    DWORD m;
    if (!g_hasConsole || !g_in || g_in == INVALID_HANDLE_VALUE) return 1;
    if (!GetConsoleMode(g_in, &m)) return 1;
    ConWriteF(L"%ls [Y/N]: ", prompt);
    FlushConsoleInputBuffer(g_in);
    if (!ReadConsoleW(g_in, buf, 31, &rd, NULL)) return 1;
    buf[rd < 31 ? rd : 31] = 0;
    return (buf[0] == L'y' || buf[0] == L'Y' || buf[0] == L'是');
}

/* ============================================================ 日志 */

void LogLine(const WCHAR *fmt, ...)
{
    WCHAR buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 4096, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (g_guiMode) { sbAdd(&g_logBuf, buf); sbAddCh(&g_logBuf, L'\r'); sbAddCh(&g_logBuf, L'\n'); }
    else ConWrite(buf);
}

/* ============================================================ 提权 */

BOOL wt_isElevated(void)
{
    HANDLE tok = NULL;
    TOKEN_ELEVATION el;
    DWORD cb = sizeof(el);
    BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return FALSE;
    if (GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &cb)) ok = el.TokenIsElevated ? TRUE : FALSE;
    CloseHandle(tok);
    return ok;
}

/* 以管理员重新启动自己；成功则返回 TRUE（调用方应立刻退出） */
BOOL wt_relaunchAsAdmin(const WCHAR *args)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH * 3];
    SHELLEXECUTEINFOW sei;
    HINSTANCE r;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf_s(cmd, MAX_PATH * 3, _TRUNCATE, L"%s\"%s\" %s", L"", exe, args ? args : L"");
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = args ? args : L"";
    sei.lpDirectory = NULL;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        (void)cmd; (void)r;
        return FALSE;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return TRUE;
}

/* ============================================================ ini 解析 */

void IniFree(IniFile *f)
{
    int i;
    if (!f) return;
    for (i = 0; i < f->n; i++) { free(f->p[i].sec); free(f->p[i].key); free(f->p[i].val); }
    free(f->p);
    f->p = NULL;
    f->n = 0;
}

static void iniPush(IniFile *f, const WCHAR *sec, const WCHAR *key, const WCHAR *val)
{
    f->p = (IniPair *)wt_realloc(f->p, sizeof(IniPair) * (size_t)(f->n + 1));
    f->p[f->n].sec = wt_strdup(sec);
    f->p[f->n].key = wt_strdup(key);
    f->p[f->n].val = wt_strdup(val);
    f->n++;
}

void IniAdd(IniFile *f, const WCHAR *sec, const WCHAR *key, const WCHAR *val)
{
    iniPush(f, sec, key, val);
}

/* 读文本文件并智能识别编码（UTF-8 BOM / UTF-16 / ANSI） */
static WCHAR *readTextAuto(const WCHAR *path)
{
    BYTE *raw = NULL;
    DWORD n = 0;
    WCHAR *w = NULL;
    if (!wt_readAll(path, &raw, &n)) return NULL;

    if (n >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {           /* UTF-16LE */
        size_t c = (n - 2) / 2;
        w = (WCHAR *)wt_alloc((c + 1) * sizeof(WCHAR));
        memcpy(w, raw + 2, c * sizeof(WCHAR));
        w[c] = 0;
    } else if (n >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {      /* UTF-16BE */
        size_t c = (n - 2) / 2, i;
        w = (WCHAR *)wt_alloc((c + 1) * sizeof(WCHAR));
        for (i = 0; i < c; i++) w[i] = (WCHAR)((raw[2 + i * 2] << 8) | raw[3 + i * 2]);
        w[c] = 0;
    } else {
        wt_utf8_to_utf16((const char *)raw, (int)n, &w);          /* 内含 BOM 跳过 + ANSI 回退 */
    }
    free(raw);
    return w;
}

BOOL IniLoad(const WCHAR *path, IniFile *f)
{
    WCHAR *text, *line, *next;
    WCHAR sec[256];
    f->p = NULL;
    f->n = 0;
    wt_strlcpy(sec, L"General", 256);

    text = readTextAuto(path);
    if (!text) return FALSE;

    line = text;
    while (line && *line) {
        WCHAR *nl = wcschr(line, L'\n');
        if (nl) { *nl = 0; next = nl + 1; } else next = NULL;

        line = wt_trim(line);
        if (*line && *line != L';' && *line != L'#') {
            if (*line == L'[') {
                WCHAR *rb = wcschr(line, L']');
                if (rb) {
                    WCHAR *s;
                    *rb = 0;
                    s = wt_trim(line + 1);
                    wt_strlcpy(sec, s, 256);
                }
            } else {
                WCHAR *eq = wcschr(line, L'=');
                if (eq && eq != line) {
                    WCHAR *k, *v, *ci;
                    *eq = 0;
                    k = wt_trim(line);
                    v = wt_trim(eq + 1);
                    /* 去掉行尾注释（; 之后） */
                    ci = wcschr(v, L';');
                    if (ci) { *ci = 0; v = wt_trim(v); }
                    if (*k) iniPush(f, sec, k, v);
                }
            }
        }
        line = next;
    }
    free(text);
    return TRUE;
}

const WCHAR *IniStr(const IniFile *f, const WCHAR *sec, const WCHAR *key)
{
    int i;
    for (i = 0; i < f->n; i++)
        if (wt_ieq(f->p[i].sec, sec) && wt_ieq(f->p[i].key, key)) return f->p[i].val;
    return NULL;
}

int IniBool(const IniFile *f, const WCHAR *sec, const WCHAR *key, int def)
{
    const WCHAR *v = IniStr(f, sec, key);
    if (!v) return def;
    if (wt_ieq(v, L"1") || wt_ieq(v, L"true") || wt_ieq(v, L"yes") || wt_ieq(v, L"开") || wt_ieq(v, L"是")) return 1;
    if (wt_ieq(v, L"0") || wt_ieq(v, L"false") || wt_ieq(v, L"no") || wt_ieq(v, L"关") || wt_ieq(v, L"否")) return 0;
    return def;
}

void *_unused_util_dummy(void) { return NULL; }
