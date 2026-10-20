<#
.SYNOPSIS
Runs a command on a cleared desktop: minimizes every other application window
first and restores them, in their original order and state, afterwards.

.DESCRIPTION
Desktop tests need their windows unobstructed and in the foreground, and some
of them send real input (SendInput) that must not reach another application.
The windows' handles are saved to out/test-results/quiet-desktop-windows.json
before anything is minimized, so an interrupted run can still be undone with
-Restore.

.EXAMPLE
pwsh tools/tests/quiet-desktop.ps1 pwsh tools/build.ps1 -Configuration Release -Test

.EXAMPLE
pwsh tools/tests/quiet-desktop.ps1 -Restore

.EXAMPLE
pwsh tools/tests/quiet-desktop.ps1 -List
#>
[CmdletBinding()]
param(
    [switch]$Restore,
    # Only lists the windows a run would minimize.
    [switch]$List,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Command
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskStateFile = Join-Path $taskRepository 'out/test-results/quiet-desktop-windows.json'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class QuietDesktop {
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] static extern long GetWindowLongPtrW(IntPtr window, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    public static uint ProcessId(IntPtr window) { uint process; GetWindowThreadProcessId(window, out process); return process; }
    public static string Title(IntPtr window) { var text = new StringBuilder(256); GetWindowTextW(window, text, text.Capacity); return text.ToString(); }
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);

    static readonly HashSet<string> shellClasses = new HashSet<string> {
        "Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd"
    };

    // Visible, unowned application windows from the top of the Z order down.
    public static List<long> ApplicationWindows() {
        var result = new List<long>();
        EnumWindows((window, parameter) => {
            if (!IsWindowVisible(window) || IsIconic(window)) return true;
            if (GetWindow(window, 4 /* GW_OWNER */) != IntPtr.Zero) return true;
            if ((GetWindowLongPtrW(window, -20 /* GWL_EXSTYLE */) & 0x80 /* WS_EX_TOOLWINDOW */) != 0) return true;
            int cloaked;
            if (DwmGetWindowAttribute(window, 14 /* DWMWA_CLOAKED */, out cloaked, 4) == 0 && cloaked != 0) return true;
            var name = new StringBuilder(256);
            GetClassNameW(window, name, name.Capacity);
            if (shellClasses.Contains(name.ToString())) return true;
            result.Add(window.ToInt64());
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
'@

function Restore-Windows {
    if (-not (Test-Path -LiteralPath $taskStateFile)) { return }
    $state = Get-Content -LiteralPath $taskStateFile -Raw | ConvertFrom-Json
    # Bottom first, so the original top window ends up on top again. SW_RESTORE
    # brings a window back to its previous state, maximized ones included.
    $windows = @($state.windows)
    [array]::Reverse($windows)
    foreach ($window in $windows) {
        $handle = [IntPtr][long]$window
        if ([QuietDesktop]::IsWindow($handle) -and [QuietDesktop]::IsIconic($handle)) {
            [void][QuietDesktop]::ShowWindowAsync($handle, 9) # SW_RESTORE
        }
    }
    $foreground = [IntPtr][long]$state.foreground
    if ($foreground -ne [IntPtr]::Zero -and [QuietDesktop]::IsWindow($foreground)) {
        [void][QuietDesktop]::SetForegroundWindow($foreground)
    }
    Remove-Item -LiteralPath $taskStateFile
}

if ($Restore) { Restore-Windows; return }
if ($List) {
    foreach ($window in [QuietDesktop]::ApplicationWindows()) {
        $process = Get-Process -Id ([QuietDesktop]::ProcessId([IntPtr]$window)) -ErrorAction SilentlyContinue
        '0x{0:x} {1}: {2}' -f $window, $process.ProcessName, [QuietDesktop]::Title([IntPtr]$window)
    }
    return
}
if (-not $Command) { throw 'Pass the command to run, or -Restore.' }
if (Test-Path -LiteralPath $taskStateFile) {
    throw "A previous run did not restore its windows. Run with -Restore first: $taskStateFile"
}

New-Item -ItemType Directory -Path (Split-Path -Parent $taskStateFile) -Force | Out-Null
$taskWindows = [QuietDesktop]::ApplicationWindows()
@{ foreground = [QuietDesktop]::GetForegroundWindow().ToInt64(); windows = @($taskWindows) } |
    ConvertTo-Json | Set-Content -LiteralPath $taskStateFile -Encoding utf8
$taskExitCode = 1
try {
    foreach ($window in $taskWindows) {
        [void][QuietDesktop]::ShowWindowAsync([IntPtr]$window, 7) # SW_SHOWMINNOACTIVE
    }
    Start-Sleep -Milliseconds 500
    $taskProgram = $Command[0]
    $taskArguments = @($Command | Select-Object -Skip 1)
    & $taskProgram @taskArguments
    $taskExitCode = $LASTEXITCODE
} finally {
    Restore-Windows
}
exit $taskExitCode
