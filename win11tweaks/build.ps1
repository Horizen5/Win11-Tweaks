# ============================================================================
#  build.ps1  --  Win11Tweaks 一键构建
#  ----------------------------------------------------------------------------
#  用本机已装的 MSVC BuildTools 2022 直接调 cl.exe，不依赖 vcvars / cmd.exe。
#  产出：dist\Win11Tweaks.exe  （静态链接 CRT，零运行时依赖）
#
#  用法：  powershell -ExecutionPolicy Bypass -File build.ps1
#          powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
# ============================================================================
param([switch]$Clean, [switch]$Trace)

$ErrorActionPreference = 'Stop'

# ---- 定位工具链 ----------------------------------------------------------
$vcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'

$msvc = Get-ChildItem $vcRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
if (-not $msvc) { throw "找不到 MSVC 工具目录：$vcRoot" }
$m = $msvc.FullName

$sdk = Get-ChildItem "$sdkRoot\Include" -Directory -ErrorAction SilentlyContinue |
       Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw "找不到 Windows SDK：$sdkRoot\Include" }
$sdkv = $sdk.Name

$clx = Join-Path $m 'bin\Hostx64\x64\cl.exe'
if (-not (Test-Path $clx)) { throw "找不到 cl.exe：$clx" }

# ---- 环境变量（不走 vcvars，因为 cmd.exe 在本机受限） ---------------------
$env:INCLUDE = "$m\include;$sdkRoot\Include\$sdkv\ucrt;$sdkRoot\Include\$sdkv\um;$sdkRoot\Include\$sdkv\shared"
$env:LIB     = "$m\lib\x64;$sdkRoot\Lib\$sdkv\ucrt\x64;$sdkRoot\Lib\$sdkv\um\x64"
# 链接器处理 /MANIFESTINPUT 时要调用 mt.exe，必须把 SDK 的 bin 放进 PATH
# （我们不走 vcvars，所以这条路要自己铺）
$env:PATH    = "$sdkRoot\bin\$sdkv\x64;$m\bin\Hostx64\x64;$env:PATH"

Write-Host "MSVC   : $m"
Write-Host "SDK    : $sdkv"

# ---- 目录 ----------------------------------------------------------------
$root = $PSScriptRoot
$src  = Join-Path $root 'src'
$bld  = Join-Path $root 'build'
$dst  = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $bld, $dst | Out-Null
if ($Clean) { Remove-Item (Join-Path $bld '*') -Recurse -Force -ErrorAction SilentlyContinue }

$files = @('util.c','reg.c','tweaks.c','profile.c','cli.c','gui.c','main.c') |
         ForEach-Object { Join-Path $src $_ }
foreach ($f in $files) { if (-not (Test-Path $f)) { throw "缺少源文件：$f" } }

$outExe = Join-Path $dst 'Win11Tweaks.exe'

# ---- 编译 ---------------------------------------------------------------
$args = @(
    '/nologo', '/utf-8', '/std:c11', '/W3', '/O2', '/Oi', '/GL',
    '/MT',                      # 静态链接 CRT -> 零运行时依赖
    '/DUNICODE', '/D_UNICODE'
)
# 性能埋点默认完全编掉（正式版连函数调用都没有）；要排错时用 -Trace
if ($Trace) { $args += '/DWT_ENABLE_TRACE' }
$args += $files
$args += @(
    "/Fe:$outExe",
    "/Fd:$(Join-Path $bld 'wt.pdb')",
    '/link',
    '/SUBSYSTEM:CONSOLE',       # 必须 CONSOLE：PowerShell 会按子系统决定是否等待进程，
                                # WINDOWS 子系统会导致 `> out.txt` 空文件、拿不到退出码。
                                # 图形界面形态由代码里的 FreeConsole() 摘掉控制台。
    '/MANIFEST:EMBED',          # 清单必须嵌进 exe（否则不是单文件）
    "/MANIFESTINPUT:$(Join-Path $src 'app.manifest')",
    '/LTCG', '/OPT:REF', '/OPT:ICF', '/INCREMENTAL:NO',
    'kernel32.lib', 'user32.lib', 'advapi32.lib', 'shell32.lib', 'gdi32.lib', 'comdlg32.lib'
)

Push-Location $bld
try {
    $log = & $clx @args 2>&1 | Out-String
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($code -ne 0) {
    Write-Host $log
    throw "编译失败，退出码 $code"
}
if ($log.Trim()) { Write-Host $log }

# 清单已经嵌入 exe 了；链接器额外落盘的那份是冗余的，删掉以保持“单文件”干净
Remove-Item "$outExe.manifest" -ErrorAction SilentlyContinue

$fi = Get-Item $outExe
Write-Host ''
Write-Host ("构建成功：{0}" -f $fi.FullName)
Write-Host ("体积    ：{0:N0} 字节 ({1:N1} KB)" -f $fi.Length, ($fi.Length / 1KB))
