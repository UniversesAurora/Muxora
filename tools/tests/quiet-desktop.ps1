<#
.SYNOPSIS
Runs a command after clearing application windows, then restores their state.

.DESCRIPTION
The snapshot is written before changing windows, so -Restore can recover an
interrupted run. Owned windows and application tool windows are included;
shell surfaces, tooltips and off-screen helper windows are left alone.
Invoke this script through a separate pwsh process. The whole session (window
cleanup, the test command with all its descendants, and recovery) runs with
administrator rights: an ordinary-privilege test cannot move the cursor or send
input while a window of a higher-privilege program is in the foreground. From
an ordinary session, -Elevate starts the session once through Windows UAC and
relays its output and exit code. Recovery starts only after the complete test
process tree has ended; an interrupted session keeps the snapshot for -Restore.

Options are matched by their full name and must precede the test command. The
first argument that is not an option starts the command, and everything after
it is passed on unchanged, so the command may use names such as -Test freely.
A command that itself starts with '-' follows a '--' separator.

.EXAMPLE
pwsh tools/tests/quiet-desktop.ps1 -Elevate pwsh tools/build.ps1 -Configuration Release -Test

.EXAMPLE
pwsh tools/tests/quiet-desktop.ps1 -Elevate -Restore
#>
$ErrorActionPreference = 'Stop'

function ConvertFrom-QuietDesktopArguments([object[]]$Arguments) {
    $options = [ordered]@{ ProcessHelpersOnly = $false; Restore = $false; List = $false; Elevate = $false
        StateFile = $null; WindowProcessId = [uint32]0; TestCommand = @() }
    $index = 0
    while ($index -lt $Arguments.Count) {
        $argument = [string]$Arguments[$index]
        if ($argument -eq '--') { ++$index; break }
        if (-not $argument.StartsWith('-')) { break }
        $name = $argument.Substring(1)
        if ($name -in @('ProcessHelpersOnly', 'Restore', 'List', 'Elevate')) {
            $options[$name] = $true
            ++$index
        } elseif ($name -in @('StateFile', 'WindowProcessId')) {
            if ($index + 1 -ge $Arguments.Count) { throw "Option $argument needs a value." }
            $options[$name] = if ($name -eq 'WindowProcessId') { [uint32]$Arguments[$index + 1] } else { [string]$Arguments[$index + 1] }
            $index += 2
        } else {
            throw "Unknown option $argument. Options go before the test command; put -- before a command that starts with '-'."
        }
    }
    $options.TestCommand = @($Arguments | Select-Object -Skip $index | ForEach-Object { [string]$_ })
    [pscustomobject]$options
}

# PowerShell would bind any prefix of a declared parameter (-Test for
# -TestCommand), so the script takes no param() block and parses $args itself.
$taskOptions = ConvertFrom-QuietDesktopArguments $args
$ProcessHelpersOnly = $taskOptions.ProcessHelpersOnly
$Restore = $taskOptions.Restore
$List = $taskOptions.List
$Elevate = $taskOptions.Elevate
$StateFile = $taskOptions.StateFile
$WindowProcessId = $taskOptions.WindowProcessId
$TestCommand = $taskOptions.TestCommand

# Internal lifecycle helpers. The caller runs in a dedicated pwsh process:
# Windows job membership cannot be removed from a live process.
if (-not ('QuietDesktopProcessScope' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

public sealed class QuietDesktopProcessScope : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Accounting {
        public long UserTime, KernelTime, PeriodUserTime, PeriodKernelTime;
        public uint PageFaults, TotalProcesses, ActiveProcesses, TerminatedProcesses;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateJobObjectW(IntPtr attributes, string name);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(SafeFileHandle job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool QueryInformationJobObject(SafeFileHandle job, int informationClass,
        out Accounting information, uint length, out uint returned);

    readonly SafeFileHandle job;

    public QuietDesktopProcessScope() {
        job = CreateJobObjectW(IntPtr.Zero, null);
        if (job.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        // Assign before invoking the command: all directly created descendants
        // inherit membership, even if their immediate parent exits very quickly.
        // No kill-on-close or breakaway limits; existing applications never join.
        if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
            int error = Marshal.GetLastWin32Error();
            job.Dispose();
            throw new Win32Exception(error, "Cannot track the desktop test process tree; no test command was started.");
        }
    }

    public void WaitForChildren() {
        while (true) {
            Accounting information;
            uint returned;
            if (!QueryInformationJobObject(job, 1 /* JobObjectBasicAccountingInformation */,
                    out information, (uint)Marshal.SizeOf<Accounting>(), out returned))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Cannot confirm desktop tests have ended; windows will remain cleared and the recovery snapshot is retained.");
            if (information.ActiveProcesses == 1) return; // This runner only.
            Thread.Sleep(25);
        }
    }

    public void Dispose() { job.Dispose(); }
}
'@
}

function Resolve-QuietDesktopProgram([string]$Name) {
    # Several matches (the same program in two PATH directories) come back as
    # an array; run the first, which is the one PowerShell itself would run.
    $program = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $program) {
        throw 'The desktop test command must be an executable; run scripts through pwsh -File.'
    }
    $program.Source
}

function Invoke-QuietDesktopSession {
    param(
        [Parameter(Mandatory)][scriptblock]$Prepare,
        [Parameter(Mandatory)][string[]]$TestCommand,
        [Parameter(Mandatory)][scriptblock]$Recover
    )
    $taskScope = $null
    $taskCommandExit = 1
    try {
        $taskProgram = Resolve-QuietDesktopProgram $TestCommand[0]
        & $Prepare
        $taskScope = [QuietDesktopProcessScope]::new()
        $taskArguments = @($TestCommand | Select-Object -Skip 1)
        $global:LASTEXITCODE = 0
        & $taskProgram @taskArguments
        $taskCommandExit = $LASTEXITCODE
    } finally {
        try {
            # A command returning is insufficient: GUI children and helpers may
            # still be alive. Recovery is unreachable until every child exits.
            # If this check fails, leave the snapshot for explicit -Restore.
            if ($taskScope) { $taskScope.WaitForChildren() }
            & $Recover
        } finally {
            if ($taskScope) { $taskScope.Dispose() }
        }
    }
    $global:LASTEXITCODE = $taskCommandExit
}

if ($ProcessHelpersOnly) { return }

$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskStateFile = Join-Path $taskRepository 'out/test-results/quiet-desktop-windows.json'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally { $identity.Dispose() }
}

function Invoke-ElevatedSession([string[]]$Arguments) {
    # One UAC prompt starts this script again with administrator rights for the
    # whole session. Its output is relayed here and its exit code returned.
    $transfer = Join-Path $taskRepository ('out/test-results/quiet-desktop-elevation-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $transfer -Force | Out-Null
    $request = Join-Path $transfer 'arguments.json'
    $output = Join-Path $transfer 'output.txt'
    $result = Join-Path $transfer 'result.json'
    ConvertTo-Json -InputObject @($Arguments) | Set-Content -LiteralPath $request -Encoding utf8
    $launch = @'
$ErrorActionPreference = 'Stop'
$taskCode = 1
try {
    Set-Location -LiteralPath '__DIRECTORY__'
    $taskArguments = @(Get-Content -LiteralPath '__REQUEST__' -Raw | ConvertFrom-Json)
    $global:LASTEXITCODE = 0
    & '__SCRIPT__' @taskArguments *>&1 | Out-File -LiteralPath '__OUTPUT__' -Encoding utf8
    $taskCode = $LASTEXITCODE
} catch {
    $_ | Out-String | Out-File -LiteralPath '__OUTPUT__' -Encoding utf8 -Append
} finally {
    @{ exitCode = $taskCode } | ConvertTo-Json | Set-Content -LiteralPath '__RESULT__' -Encoding utf8
}
exit $taskCode
'@
    $launch = $launch.Replace('__DIRECTORY__', (Get-Location).ProviderPath.Replace("'", "''")).
        Replace('__SCRIPT__', $PSCommandPath.Replace("'", "''")).
        Replace('__REQUEST__', $request.Replace("'", "''")).
        Replace('__OUTPUT__', $output.Replace("'", "''")).
        Replace('__RESULT__', $result.Replace("'", "''"))
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($launch))
    $reader = $null
    $process = $null
    try {
        $process = Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -Verb RunAs -WindowStyle Hidden -PassThru `
            -ArgumentList @('-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)
        do {
            $exited = $process.WaitForExit(200)
            if (-not $reader -and (Test-Path -LiteralPath $output)) {
                $stream = [IO.File]::Open($output, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
                $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
            }
            if ($reader) {
                $text = $reader.ReadToEnd()
                if ($text) { Write-Host -NoNewline $text }
            }
        } until ($exited)
        if (-not (Test-Path -LiteralPath $result)) { throw 'The elevated desktop session ended without reporting its exit code.' }
        [int](Get-Content -LiteralPath $result -Raw | ConvertFrom-Json).exitCode
    } finally {
        if ($reader) { $reader.Dispose() }
        if ($process) { $process.Dispose() }
        $resolved = [IO.Path]::GetFullPath($transfer)
        $allowed = [IO.Path]::GetFullPath((Join-Path $taskRepository 'out/test-results')) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected elevation transfer path.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

if ($Elevate -and -not (Test-Administrator)) {
    # Everything except -Elevate is passed on unchanged.
    $taskForwarded = [Collections.Generic.List[string]]::new()
    foreach ($taskName in @('Restore', 'List')) { if ($taskOptions.$taskName) { $taskForwarded.Add("-$taskName") } }
    if ($StateFile) { $taskForwarded.Add('-StateFile'); $taskForwarded.Add($StateFile) }
    if ($WindowProcessId -ne 0) { $taskForwarded.Add('-WindowProcessId'); $taskForwarded.Add([string]$WindowProcessId) }
    if ($TestCommand.Count) { $taskForwarded.Add('--'); foreach ($taskPart in $TestCommand) { $taskForwarded.Add($taskPart) } }
    exit (Invoke-ElevatedSession $taskForwarded.ToArray())
}

if ($StateFile) {
    $taskStateFile = [IO.Path]::GetFullPath($StateFile)
    $taskAllowed = [IO.Path]::GetFullPath((Join-Path $taskRepository 'out/test-results')) + [IO.Path]::DirectorySeparatorChar
    if (-not $taskStateFile.StartsWith($taskAllowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'An isolated fixture recovery path must be inside out/test-results.'
    }
} elseif ($WindowProcessId -ne 0) {
    throw 'A fixture process scope requires a separate recovery path.'
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class QuietDesktopNative {
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Placement {
        public int Length, Flags, ShowCommand;
        public Point MinPosition, MaxPosition;
        public Rect Normal;
    }
    public sealed class WindowState {
        public long Handle;
        public uint Process, Thread;
        public string Class;
        public long Owner;
        public bool Topmost;
        public Placement Placement;
    }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtrW(IntPtr window, int index);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern bool GetWindowPlacement(IntPtr window, ref Placement placement);
    [DllImport("user32.dll")] static extern bool SetWindowPlacement(IntPtr window, ref Placement placement);
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] static extern bool GetLayeredWindowAttributes(IntPtr window, out uint key, out byte alpha, out uint flags);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);
    [DllImport("dwmapi.dll")] static extern int DwmFlush();
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SendMessageTimeoutW(
        IntPtr window, uint message, IntPtr first, IntPtr second, uint flags, uint timeout, out UIntPtr result);

    static readonly HashSet<string> excludedClasses = new HashSet<string>(StringComparer.OrdinalIgnoreCase) {
        "Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "#32769",
        "tooltips_class32", "SysShadow", "SysDragImage", "#32768", "IME", "MSCTFIME UI"
    };

    public static string ClassName(IntPtr window) {
        var name = new StringBuilder(256);
        GetClassNameW(window, name, name.Capacity);
        return name.ToString();
    }

    public static bool IsApplicationWindow(IntPtr window) {
        var name = ClassName(window);
        if (!IsWindowVisible(window) || IsIconic(window) || excludedClasses.Contains(name) ||
            name.IndexOf("QToolTip", StringComparison.OrdinalIgnoreCase) >= 0 ||
            name.IndexOf("ToolTipSaveBits", StringComparison.OrdinalIgnoreCase) >= 0) return false;
        int cloaked;
        if (DwmGetWindowAttribute(window, 14 /* DWMWA_CLOAKED */, out cloaked, 4) == 0 && cloaked != 0) return false;
        Rect rect;
        if (!GetWindowRect(window, out rect) || rect.Right - rect.Left <= 1 || rect.Bottom - rect.Top <= 1 ||
            MonitorFromWindow(window, 0 /* MONITOR_DEFAULTTONULL */) == IntPtr.Zero) return false;
        uint key, flags;
        byte alpha;
        if (GetLayeredWindowAttributes(window, out key, out alpha, out flags) && (flags & 2) != 0 && alpha == 0) return false;
        return true;
    }

    public static WindowState[] Capture(uint processId = 0) {
        var result = new List<WindowState>();
        EnumWindows((window, parameter) => {
            if (!IsApplicationWindow(window)) return true;
            uint process;
            uint thread = GetWindowThreadProcessId(window, out process);
            if (processId != 0 && process != processId) return true;
            var placement = new Placement { Length = Marshal.SizeOf<Placement>() };
            if (!GetWindowPlacement(window, ref placement))
                throw new InvalidOperationException("Cannot save an application window's placement.");
            result.Add(new WindowState {
                Handle = window.ToInt64(), Process = process, Thread = thread, Class = ClassName(window),
                Owner = GetWindow(window, 4 /* GW_OWNER */).ToInt64(),
                Topmost = (GetWindowLongPtrW(window, -20 /* GWL_EXSTYLE */).ToInt64() & 8) != 0,
                Placement = placement
            });
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }

    public static bool SameWindow(WindowState state) {
        var window = new IntPtr(state.Handle);
        uint process;
        return IsWindow(window) && GetWindowThreadProcessId(window, out process) == state.Thread &&
            process == state.Process && ClassName(window) == state.Class;
    }

    public static WindowState Identity(long handle) {
        uint process;
        var window = new IntPtr(handle);
        uint thread = GetWindowThreadProcessId(window, out process);
        return new WindowState { Handle = handle, Process = process, Thread = thread, Class = ClassName(window) };
    }

    public static bool Cleared(WindowState state) {
        var window = new IntPtr(state.Handle);
        return !SameWindow(state) || !IsWindowVisible(window) || IsIconic(window);
    }

    static bool Wait(WindowState[] states, Func<WindowState, bool> predicate, int milliseconds) {
        var elapsed = Stopwatch.StartNew();
        do {
            bool complete = true;
            foreach (var state in states) if (!predicate(state)) { complete = false; break; }
            if (complete) return true;
            Thread.Sleep(25);
        } while (elapsed.ElapsedMilliseconds < milliseconds);
        return false;
    }

    public static void Clear(WindowState[] states) {
        // Owners generally precede their owned windows in bottom-to-top order.
        for (int i = states.Length - 1; i >= 0; --i)
            if (SameWindow(states[i]) && !Cleared(states[i])) {
                var window = new IntPtr(states[i].Handle);
                bool nonActivating = (GetWindowLongPtrW(window, -20).ToInt64() & 0x08000000) != 0;
                bool canMinimize = (GetWindowLongPtrW(window, -16).ToInt64() & 0x00020000) != 0;
                ShowWindowAsync(window, nonActivating && !canMinimize ? 0 : 7);
            }
        if (Wait(states, Cleared, 2000)) return;
        // Some palettes and tray applications refuse minimization. Hide those
        // temporarily; the original placement is already safely recorded.
        foreach (var state in states)
            if (!Cleared(state)) ShowWindowAsync(new IntPtr(state.Handle), 0);
        if (!Wait(states, Cleared, 3000)) {
            var remaining = new List<string>();
            foreach (var state in states) if (!Cleared(state))
                remaining.Add("0x" + state.Handle.ToString("x") + " PID " + state.Process + " " + state.Class);
            throw new InvalidOperationException("Application windows could not be cleared; the test command was not started. Windows at a higher privilege level may require an authorized elevated run or manual minimization: " + string.Join("; ", remaining));
        }
    }

    public static void WaitUntilReady(WindowState[] states, uint processId) {
        // IsIconic can change before the target thread finishes WM_SIZE. Drain
        // each recorded UI thread before allowing any fixture to be created.
        var elapsed = Stopwatch.StartNew();
        long settledAt = -1;
        do {
            bool ready = true;
            var threads = new HashSet<uint>();
            foreach (var state in states) {
                if (!SameWindow(state)) continue;
                UIntPtr result;
                if (!Cleared(state) || (threads.Add(state.Thread) &&
                    SendMessageTimeoutW(new IntPtr(state.Handle), 0 /* WM_NULL */,
                        IntPtr.Zero, IntPtr.Zero, 0x23 /* BLOCK | ABORTIFHUNG | ERRORONEXIT */,
                        50, out result) == IntPtr.Zero)) { ready = false; break; }
            }
            if (Capture(processId).Length != 0) {
                // Do not touch windows absent from the frozen initial snapshot.
                throw new InvalidOperationException("Application windows appeared after the initial snapshot; the test command was not started.");
            }
            if (ready) {
                DwmFlush(); // Present barrier supplements the per-thread barrier.
                if (settledAt < 0) settledAt = elapsed.ElapsedMilliseconds;
                if (elapsed.ElapsedMilliseconds - settledAt >= 250) return;
            } else settledAt = -1;
            Thread.Sleep(25);
        } while (elapsed.ElapsedMilliseconds < 5000);
        throw new InvalidOperationException("Application window minimization did not settle; the test command was not started.");
    }

    public static void Restore(WindowState[] states, WindowState foreground) {
        for (int i = states.Length - 1; i >= 0; --i) {
            var state = states[i];
            if (!SameWindow(state)) continue;
            var placement = state.Placement;
            placement.Length = Marshal.SizeOf<Placement>();
            placement.Flags |= 4 /* WPF_ASYNCWINDOWPLACEMENT */;
            if (placement.ShowCommand == 1) placement.ShowCommand = 4 /* SW_SHOWNOACTIVATE */;
            SetWindowPlacement(new IntPtr(state.Handle), ref placement);
        }
        if (!Wait(states, state => !SameWindow(state) ||
                PlacementRestored(state), 5000))
            throw new InvalidOperationException("Some application windows have not restored; keep the recovery snapshot and run -Restore again.");
        if (foreground != null && SameWindow(foreground)) SetForegroundWindow(new IntPtr(foreground.Handle));
        RestoreOrder(states);
    }

    static bool Topmost(WindowState state) {
        return (GetWindowLongPtrW(new IntPtr(state.Handle), -20).ToInt64() & 8) != 0;
    }

    static WindowState Predecessor(WindowState state, WindowState[] states) {
        WindowState previous = null;
        foreach (var candidate in states) {
            if (candidate == state) break;
            if (candidate.Topmost == state.Topmost && SameWindow(candidate)) previous = candidate;
        }
        return previous;
    }

    static int OwnerDepth(WindowState state) {
        int depth = 0;
        var visited = new HashSet<long>();
        var owner = GetWindow(new IntPtr(state.Handle), 4);
        while (owner != IntPtr.Zero && visited.Add(owner.ToInt64())) {
            ++depth;
            owner = GetWindow(owner, 4);
        }
        return depth;
    }

    static bool Before(WindowState state, WindowState previous) {
        if (previous == null || !SameWindow(state) || !SameWindow(previous)) return false;
        bool before = false;
        EnumWindows((window, parameter) => {
            if (window.ToInt64() == previous.Handle) return false;
            if (window.ToInt64() == state.Handle) { before = true; return false; }
            return true;
        }, IntPtr.Zero);
        return before;
    }

    static void RestoreOrder(WindowState[] states) {
        const uint flags = 0x0013 /* NOACTIVATE | NOMOVE | NOSIZE */ | 0x0200 /* NOOWNERZORDER */ | 0x4000 /* ASYNCWINDOWPOS */;
        var ownersFirst = new List<WindowState>(states);
        ownersFirst.Sort((left, right) => OwnerDepth(left).CompareTo(OwnerDepth(right)));
        var elapsed = Stopwatch.StartNew();
        long settledAt = -1;
        do {
            bool bandsMatch = true;
            foreach (var state in ownersFirst) {
                if (!SameWindow(state) || Topmost(state) == state.Topmost) continue;
                bandsMatch = false;
                SetWindowPos(new IntPtr(state.Handle), new IntPtr(state.Topmost ? -1 : -2), 0, 0, 0, 0, flags);
            }
            foreach (var state in states) {
                if (!SameWindow(state)) continue;
                var previous = Predecessor(state, states);
                // Raising another application's first window needs foreground
                // permission. Put only out-of-order peers down behind their
                // live predecessor; later peers close any temporary gaps.
                if (Before(state, previous))
                    SetWindowPos(new IntPtr(state.Handle), new IntPtr(previous.Handle), 0, 0, 0, 0, flags);
            }
            if (bandsMatch && OrderRestored(states)) {
                if (settledAt < 0) settledAt = elapsed.ElapsedMilliseconds;
                if (elapsed.ElapsedMilliseconds - settledAt >= 100) return;
            } else settledAt = -1;
            Thread.Sleep(25);
        } while (elapsed.ElapsedMilliseconds < 3000);
        foreach (var state in states) {
            if (!SameWindow(state)) continue;
            var previous = Predecessor(state, states);
            if (Topmost(state) != state.Topmost || Before(state, previous))
                throw new InvalidOperationException("Application window order has not restored; keep the recovery snapshot and run -Restore again. " + PositionDiagnostic(state, previous, states));
        }
        throw new InvalidOperationException("Application window order did not settle; keep the recovery snapshot and run -Restore again.");
    }

    static bool PlacementRestored(WindowState state) {
        var window = new IntPtr(state.Handle);
        if (!IsWindowVisible(window) || IsIconic(window)) return false;
        var current = new Placement { Length = Marshal.SizeOf<Placement>() };
        if (!GetWindowPlacement(window, ref current)) return false;
        var saved = state.Placement;
        return current.ShowCommand == saved.ShowCommand && current.Normal.Left == saved.Normal.Left &&
            current.Normal.Top == saved.Normal.Top && current.Normal.Right == saved.Normal.Right &&
            current.Normal.Bottom == saved.Normal.Bottom;
    }

    static bool OrderRestored(WindowState[] states) {
        var expected = new List<long>();
        foreach (var state in states) if (SameWindow(state)) expected.Add(state.Handle);
        var remaining = new HashSet<long>(expected);
        var actual = new List<long>();
        EnumWindows((window, parameter) => {
            if (remaining.Contains(window.ToInt64())) actual.Add(window.ToInt64());
            return true;
        }, IntPtr.Zero);
        if (expected.Count != actual.Count) return false;
        for (int i = 0; i < expected.Count; ++i) if (expected[i] != actual[i]) return false;
        return true;
    }

    static string PositionDiagnostic(WindowState state, WindowState previous, WindowState[] states) {
        var window = new IntPtr(state.Handle);
        var peers = new HashSet<long>();
        foreach (var candidate in states) if (SameWindow(candidate)) peers.Add(candidate.Handle);
        var order = new List<string>();
        EnumWindows((entry, parameter) => {
            if (peers.Contains(entry.ToInt64())) order.Add("0x" + entry.ToInt64().ToString("x"));
            return true;
        }, IntPtr.Zero);
        return "window=0x" + state.Handle.ToString("x") + " PID=" + state.Process + " thread=" + state.Thread +
            " class=" + state.Class + " owner=0x" + GetWindow(window, 4).ToInt64().ToString("x") +
            " topmost=" + ((GetWindowLongPtrW(window, -20).ToInt64() & 8) != 0) + " expectedTopmost=" + state.Topmost +
            " previous=" + (previous == null ? "none" : "0x" + previous.Handle.ToString("x") + " alive=" + SameWindow(previous)) +
            " foreground=0x" + GetForegroundWindow().ToInt64().ToString("x") + " order=" + string.Join(",", order);
    }

    public static WindowState FromSnapshot(long handle, uint process, uint thread, string name, bool topmost,
        int flags, int show, int minX, int minY, int maxX, int maxY, int left, int top, int right, int bottom, long owner) {
        return new WindowState { Handle = handle, Process = process, Thread = thread, Class = name, Topmost = topmost, Owner = owner,
            Placement = new Placement { Length = Marshal.SizeOf<Placement>(), Flags = flags, ShowCommand = show,
                MinPosition = new Point { X = minX, Y = minY }, MaxPosition = new Point { X = maxX, Y = maxY },
                Normal = new Rect { Left = left, Top = top, Right = right, Bottom = bottom } } };
    }
}
'@

function Read-WindowSnapshot($window) {
    $place = $window.Placement
    [QuietDesktopNative]::FromSnapshot($window.Handle, $window.Process, $window.Thread, $window.Class, $window.Topmost,
        $place.Flags, $place.ShowCommand, $place.MinPosition.X, $place.MinPosition.Y,
        $place.MaxPosition.X, $place.MaxPosition.Y, $place.Normal.Left, $place.Normal.Top,
        $place.Normal.Right, $place.Normal.Bottom, $window.Owner)
}

function Restore-Windows {
    if (-not (Test-Path -LiteralPath $taskStateFile)) { return }
    $state = Get-Content -LiteralPath $taskStateFile -Raw | ConvertFrom-Json
    if ($state.version -eq 2) {
        $records = @($state.windows | ForEach-Object { Read-WindowSnapshot $_ })
        [QuietDesktopNative]::Restore([QuietDesktopNative+WindowState[]]$records, (Read-WindowSnapshot $state.foregroundWindow))
    } else {
        # Emergency recovery for the original handle-only snapshot format.
        $windows = @($state.windows)
        [array]::Reverse($windows)
        foreach ($window in $windows) {
            $handle = [IntPtr][long]$window
            if ([QuietDesktopNative]::IsWindow($handle) -and [QuietDesktopNative]::IsIconic($handle)) {
                [void][QuietDesktopNative]::ShowWindowAsync($handle, 9)
            }
        }
    }
    $foreground = [IntPtr][long]$state.foreground
    $sameForeground = $state.version -ne 2 -or [QuietDesktopNative]::SameWindow((Read-WindowSnapshot $state.foregroundWindow))
    if ($state.version -ne 2 -and $sameForeground -and $foreground -ne [IntPtr]::Zero -and [QuietDesktopNative]::IsWindow($foreground)) {
        [void][QuietDesktopNative]::SetForegroundWindow($foreground)
    }
    Remove-Item -LiteralPath $taskStateFile
}

function Write-WindowList {
    foreach ($window in [QuietDesktopNative]::Capture($WindowProcessId)) {
        $process = Get-Process -Id $window.Process -ErrorAction SilentlyContinue
        '0x{0:x} {1} ({2}, owner 0x{3:x})' -f $window.Handle, $process.ProcessName, $window.Class, $window.Owner
    }
}

function Initialize-WindowSnapshot {
    if (Test-Path -LiteralPath $taskStateFile) {
        throw "A previous run did not restore its windows. Run with -Restore first: $taskStateFile"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskStateFile) -Force | Out-Null
    $script:taskWindows = @([QuietDesktopNative]::Capture($WindowProcessId))
    $script:taskForeground = [QuietDesktopNative]::GetForegroundWindow().ToInt64()
    $script:taskForegroundWindow = [QuietDesktopNative]::Identity($taskForeground)
    if ($WindowProcessId -ne 0 -and $taskForegroundWindow.Process -ne $WindowProcessId) {
        $script:taskForeground = 0
        $script:taskForegroundWindow = [QuietDesktopNative]::Identity(0)
    }
    @{ version = 2; foreground = $taskForeground; foregroundWindow = $taskForegroundWindow; windows = @($taskWindows) } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$taskStateFile.tmp" -Encoding utf8
    Move-Item -LiteralPath "$taskStateFile.tmp" -Destination $taskStateFile -Force
}

function Clear-WindowSnapshot {
    [QuietDesktopNative]::Clear([QuietDesktopNative+WindowState[]]$taskWindows)
    $additional = @([QuietDesktopNative]::Capture($WindowProcessId))
    if ($additional.Count -ne 0) {
        # The initial snapshot is frozen: previously hidden/minimized windows
        # and windows created during cleanup are never taken over or restored.
        $identities = @($additional | ForEach-Object { '0x{0:x} PID {1} {2}' -f $_.Handle, $_.Process, $_.Class })
        throw ('Application windows appeared after the initial snapshot; the test command was not started. Close or minimize them before retrying: ' + ($identities -join '; '))
    }
    [QuietDesktopNative]::WaitUntilReady([QuietDesktopNative+WindowState[]]$taskWindows, $WindowProcessId)
}

if ($Restore -or $List) {
    if ($Restore) { Restore-Windows } else { Write-WindowList }
    return
}
if (-not $TestCommand) { throw 'Pass the command to run, or -Restore.' }
if (Test-Path -LiteralPath $taskStateFile) {
    throw "A previous run did not restore its windows. Run with -Restore first: $taskStateFile"
}

Invoke-QuietDesktopSession -Prepare {
    Initialize-WindowSnapshot
    Clear-WindowSnapshot
} -TestCommand $TestCommand -Recover { Restore-Windows }
exit $LASTEXITCODE
