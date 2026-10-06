# ============================================================================
#  run.ps1  --  Run dist\Win11Tweaks.exe with UTF-8 capture + timeout guard
#  用法:  .\run.ps1 -Cmd '--status' -OutFile 'build\status.txt'
#  说明:  参数名刻意不用 $Args（PowerShell 自动变量，会造成参数错位）。
# ============================================================================
param(
    [string]$Cmd = '--help',
    [int]$TimeoutMs = 8000,
    [string]$OutFile = ''
)

$exe = Join-Path $PSScriptRoot 'dist\Win11Tweaks.exe'
if (-not (Test-Path $exe)) { throw "Win11Tweaks.exe not found: $exe" }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = $Cmd
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.StandardOutputEncoding = [Text.Encoding]::UTF8
$psi.StandardErrorEncoding = [Text.Encoding]::UTF8

$p = [System.Diagnostics.Process]::Start($psi)
$tOut = $p.StandardOutput.ReadToEndAsync()
$tErr = $p.StandardError.ReadToEndAsync()

if (-not $p.WaitForExit($TimeoutMs)) {
    "[TIMEOUT after ${TimeoutMs}ms] killing pid $($p.Id)"
    try { $p.Kill() } catch {}
    try { $p.WaitForExit(2000) | Out-Null } catch {}
}

try { $out = $tOut.Result } catch { $out = "<stdout unreadable: $($_.Exception.Message)>" }
try { $err = $tErr.Result } catch { $err = "<stderr unreadable>" }

if ($OutFile) {
    $full = if ([IO.Path]::IsPathRooted($OutFile)) { $OutFile } else { Join-Path $PSScriptRoot $OutFile }
    $txt = $out + "`r`n--- exit=" + $p.ExitCode + " ---`r`n"
    if ($err.Trim()) { $txt += "[stderr] " + $err + "`r`n" }
    [IO.File]::WriteAllText($full, $txt, (New-Object Text.UTF8Encoding $false))
    "saved: $full"
} else {
    if ($out) { $out }
    if ($err -and $err.Trim()) { "[stderr] " + $err }
    "[exit=$($p.ExitCode)]"
}
