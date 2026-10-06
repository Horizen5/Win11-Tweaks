/* ============================================================================
 *  main.c  --  程序入口：单 exe 双形态
 *  ----------------------------------------------------------------------------
 *  用 SUBSYSTEM:CONSOLE 编译。为什么不用 WINDOWS 子系统？
 *    PowerShell 是按 PE 子系统来决定"要不要等这个进程"的：
 *    对 WINDOWS 子系统的程序它甩手就走，于是命令行下
 *      · `Win11Tweaks.exe --status > out.txt` 里的 out.txt 是空的
 *      · $LASTEXITCODE 拿不到
 *      · 脚本里的 --import 会在恢复完成之前就返回
 *    （实测：裸调用 3.3ms 就返回，而真实工作量约 10ms。）
 *  所以走 CONSOLE，再在图形界面形态下把控制台摘掉：双击时那个控制台
 *  窗口只在开头的几毫秒内存在，几乎看不见；而命令行在任何宿主
 *  （cmd / PowerShell / bash / 计划任务）里行为都正确。
 *    · 无参数 或 --gui  -> 图形界面
 *    · 带任意其它参数   -> 命令行
 * ========================================================================== */
#include "wt.h"

int g_argc = 0;
WCHAR **g_argv = NULL;

typedef BOOL (WINAPI *PFN_DPIAWARE)(void);

/* 进入图形界面之前把控制台摘掉。
   ★ 只有在这个控制台是我们独占的时候才隐藏它的窗口；
     如果我们是挂在用户已有的 cmd/PowerShell 控制台上，就只脱离、
     绝不去动人家的窗口（否则会把用户的终端整个藏起来）。 */
static void DetachConsoleForGui(void)
{
    DWORD pids[8];
    DWORD n;
    HWND cw = GetConsoleWindow();
    if (!cw) return;                       /* 本来就没有控制台 */
    n = GetConsoleProcessList(pids, 8);
    if (n <= 1) {
        ShowWindow(cw, SW_HIDE);            /* 自己新建的：先藏再销毁，减少闪烁 */
        FreeConsole();
    } else {
        FreeConsole();                      /* 别人的控制台：只脱离 */
    }
}

int main(void)
{
    int argc = 0;
    WCHAR **argv;
    Ctx ctx;
    int rc = 0;
    int wantGui = 0;

    WtTrace("main.enter");

    /* 高 DPI：让字体清晰（老系统上可能没有这个导出函数，失败也无妨） */
    {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) {
            PFN_DPIAWARE f = (PFN_DPIAWARE)(void *)GetProcAddress(u, "SetProcessDPIAware");
            if (f) f();
        }
    }
    WtTrace("main.dpi");

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    WtTrace("main.argv");

    memset(&ctx, 0, sizeof(ctx));
    wt_getExeDir(ctx.exeDir, MAX_PATH);
    ctx.win11  = wt_isWin11();
    ctx.admin  = wt_isElevated();
    ctx.dryRun = FALSE;
    WtTrace("main.ctx");

    g_argc = argc;
    g_argv = argv;

    if (argc >= 2) {
        int i;
        for (i = 1; i < argc; i++)
            if (wt_ieq(argv[i], L"--gui") || wt_ieq(argv[i], L"-g")) wantGui = 1;
    }

    if (argc <= 1 || wantGui) {
        DetachConsoleForGui();
        rc = GuiMain(&ctx);
    } else {
        WtTrace("main.preConInit");
        ConInit();
        WtTrace("main.postConInit");
        rc = CliMain(&ctx, argc, argv);
        WtTrace("main.afterCli");
        if (rc == 4) {          /* CLI 里遇到 --gui，转图形界面 */
            DetachConsoleForGui();
            rc = GuiMain(&ctx);
        }
    }
    WtTrace("main.beforeReturn");

    LocalFree(argv);
    WtTrace("main.afterFreeArgv");
    return rc;
}
