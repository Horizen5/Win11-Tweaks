# ============================================================================
#  bench.ps1  --  性能实测：新原生 exe  vs  旧 PowerShell 方案
#  全程【零副作用】：
#    · 不跑原 tweaks.ps1（它会把用户自定义的壁纸换掉），用等价工作量的脚本做基准
#    · --import 用“同源配置”，差异恒为 0 项，走完整比对管线但不写入任何注册表
# ============================================================================
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$ErrorActionPreference = 'Continue'

# 路径按脚本自身位置推导，不写死用户名与盘符
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path $MyInvocation.MyCommand.Path -Parent }
$repoRoot  = Split-Path $scriptDir -Parent
$r    = $scriptDir
$exe  = Join-Path $r 'dist\Win11Tweaks.exe'
$old  = Join-Path $repoRoot 'src_win11\Win11_顺手设置_通用版'
$zip  = Join-Path $env:USERPROFILE 'Desktop\Win11_顺手设置_通用版.zip'

# --- “旧方案等价脚本”：51 次 Test-Path + Get-ItemProperty -------------------
$null = New-Item -ItemType Directory -Force -Path "$r\build" -ErrorAction SilentlyContinue
$psEquiv = "$r\build\ps_equiv.ps1"
@'
$p = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'
1..51 | ForEach-Object {
    if (Test-Path -LiteralPath $p) {
        $null = Get-ItemProperty -LiteralPath $p -Name 'TaskbarAl' -ErrorAction SilentlyContinue
    }
}
'@ | Set-Content -Path $psEquiv -Encoding UTF8

function Quote-Arg { param([string]$a)
    if ($a -notmatch '[\s"]') { return $a }
    return '"' + ($a -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

# 统一用 ProcessStartInfo：不受本机 Start-Process 别名冲突影响，且能拿到退出码
function Invoke-Exe([string[]]$a) {
    $si = New-Object System.Diagnostics.ProcessStartInfo
    $si.FileName = $exe
    $si.Arguments = (($a | ForEach-Object { Quote-Arg $_ }) -join ' ')
    $si.UseShellExecute = $false
    $si.CreateNoWindow = $true
    $si.RedirectStandardOutput = $true
    $si.RedirectStandardError = $true
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = [Diagnostics.Process]::Start($si)
    $null = $p.StandardOutput.ReadToEnd()
    $null = $p.StandardError.ReadToEnd()
    if (-not $p.WaitForExit(30000)) { $p.Kill(); $p.WaitForExit() }
    $sw.Stop()
    return [pscustomobject]@{ Ms = $sw.Elapsed.TotalMilliseconds; Exit = $p.ExitCode }
}

function Bench([string]$label, [scriptblock]$block, [int]$n = 5) {
    $t = @()
    for ($i = 0; $i -lt $n; $i++) { $t += (& $block) }
    $s = @($t | Sort-Object)
    [PSCustomObject]@{
        项目     = $label
        平均ms   = [math]::Round(($t | Measure-Object -Average).Average, 1)
        中位ms   = [math]::Round($s[[int]($s.Count / 2)], 1)
        最慢ms   = [math]::Round($s[-1], 1)
        慢于200ms = @($t | Where-Object { $_ -gt 200 }).Count
    }
}

$prof = Join-Path $r 'build\testout\bench_profile.json'
# 先导出一份当前状态作为“同源配置”：后面 --import 它的差异恒为 0 项，
# 既走完整比对管线，又保证不写入任何注册表。
Invoke-Exe @('--export', $prof) | Out-Null
if (-not (Test-Path $prof)) { throw "导出失败，无法继续基准测试" }

# ---------- 1) 新方案：逐命令 ----------
$N = 20
$res = @()
$res += Bench '新  --version'                                   { (Invoke-Exe @('--version')).Ms } $N
$res += Bench '新  --status（读 51 值 + 输出）'                  { (Invoke-Exe @('--status')).Ms } $N
$res += Bench '新  --apply --dry-run（完整应用逻辑，不落盘）'    { (Invoke-Exe @('--apply','--dry-run')).Ms } $N
$res += Bench '新  --export（写 JSON 快照）'                     { (Invoke-Exe @('--export')).Ms } $N
$res += Bench '新  --backup（写还原点）'                         { (Invoke-Exe @('--backup')).Ms } $N
$res += Bench '新  --diff（读 JSON + 比对）'                     { (Invoke-Exe @('--diff',$prof)).Ms } $N
$res += Bench '新  --import（同源配置，差异 0，零写入）'          { (Invoke-Exe @('--import',$prof,'--yes','--no-elevate')).Ms } $N

Write-Host ''
Write-Host '======================= 新原生 exe（每项 20 次）======================='
$res | Format-Table -AutoSize | Out-String | Write-Host

# ---------- 2) 旧方案基准 ----------
$oldRes = @()
$oldRes += Bench '旧  PowerShell 5.1 冷启动（空脚本）'          { $sw=[Diagnostics.Stopwatch]::StartNew(); powershell.exe -NoProfile -ExecutionPolicy Bypass -Command 'exit' 2>&1 | Out-Null; $sw.Stop(); $sw.Elapsed.TotalMilliseconds } 5
$oldRes += Bench '旧  执行 51 次注册表读取（等价工作量）'        { $sw=[Diagnostics.Stopwatch]::StartNew(); powershell.exe -NoProfile -ExecutionPolicy Bypass -File $psEquiv 2>&1 | Out-Null; $sw.Stop(); $sw.Elapsed.TotalMilliseconds } 5

Write-Host '======================= 旧 PowerShell 方案（每项 5 次）======================='
$oldRes | Format-Table -AutoSize | Out-String | Write-Host

# ---------- 3) 体积 ----------
Write-Host '======================= 体积 ======================='
$fi = Get-Item $exe
Write-Host ("新 exe（单文件，静态 CRT）: {0:N0} 字节  ({1:N1} KB)" -f $fi.Length, ($fi.Length / 1KB))
$oldFiles = Get-ChildItem $old -File
$oldSum = ($oldFiles | Measure-Object -Property Length -Sum).Sum
Write-Host ("旧方案 {0} 个文件合计       : {1:N0} 字节  ({2:N1} KB)" -f $oldFiles.Count, $oldSum, ($oldSum / 1KB))
Write-Host ("旧方案压缩包                : {0:N0} 字节  ({1:N1} KB)" -f (Get-Item $zip).Length, ((Get-Item $zip).Length / 1KB))

# ---------- 4) 依赖 ----------
Write-Host ''
Write-Host '======================= 依赖 DLL ======================='
$m = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
& "$m\bin\Hostx64\x64\dumpbin.exe" /nologo /dependents $exe 2>&1 |
    Select-String -Pattern '^\s+\S+\.dll' | ForEach-Object { Write-Host ('  ' + $_.Line.Trim()) }

# ---------- 5) GUI 内存 ----------
Write-Host ''
Write-Host '======================= 内存占用（GUI 运行时）======================='
Get-Process -Name 'Win11Tweaks' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$si = New-Object System.Diagnostics.ProcessStartInfo
$si.FileName = $exe
$si.UseShellExecute = $false
$p = [Diagnostics.Process]::Start($si)
Start-Sleep -Seconds 2
$procs = @(Get-Process -Id $p.Id -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) {
    Write-Host '  （GUI 未能启动或被策略拦住）'
} else {
    $procs | ForEach-Object {
        Write-Host ("  工作集 WorkingSet : {0:N1} MB" -f ($_.WorkingSet64 / 1MB))
        Write-Host ("  私有内存 Private  : {0:N1} MB" -f ($_.PrivateMemorySize64 / 1MB))
        Write-Host ("  线程数 / 句柄数   : {0} / {1}" -f $_.Threads.Count, $_.HandleCount)
    }
}
$procs | Stop-Process -Force
