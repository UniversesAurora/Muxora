<#
.SYNOPSIS
Checks desktop isolation and recovery using native windows and Win32 assertions.
#>
[CmdletBinding()]
param(
    [switch]$Headless,
    [string]$WorkerCase,
    [switch]$CommandChild,
    [switch]$Grandchild,
    [string[]]$Payload,
    [switch]$Probe,
    [switch]$IsolatedWorker,
    [string]$CaseDirectory,
    [string]$FixtureStateFile,
    [string]$OuterStateFile,
    [int]$ProbeExitCode = 0
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskWrapper = Join-Path $PSScriptRoot 'quiet-desktop.ps1'
$taskOuterRecovery = Join-Path $taskRepository 'out/test-results/quiet-desktop-windows.json'
# The running pwsh: Get-Command can return several when PATH lists two installs.
$taskPwsh = Join-Path $PSHOME 'pwsh.exe'

if ($Headless -or $WorkerCase -or $CommandChild -or $Grandchild) {
    $taskShell = Join-Path $PSHOME 'pwsh.exe'

    function Assert-That([bool]$Condition, [string]$Message) {
        if (-not $Condition) { throw $Message }
    }

    function Start-HiddenChild([string[]]$Arguments) {
        $start = [Diagnostics.ProcessStartInfo]::new($taskShell)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        foreach ($argument in @('-NoLogo', '-NoProfile', '-File', $PSCommandPath) + $Arguments) {
            $start.ArgumentList.Add($argument)
        }
        [Diagnostics.Process]::Start($start)
    }

    if ($Grandchild) {
        Start-Sleep -Milliseconds 200
        Set-Content -LiteralPath (Join-Path $CaseDirectory 'last-child-finished') -Value $PID
        exit 0
    }
    if ($CommandChild) {
        Assert-That (Test-Path -LiteralPath (Join-Path $CaseDirectory 'prepare-finished')) 'A command ran before preparation completed.'
        Assert-That (-not (Test-Path -LiteralPath (Join-Path $CaseDirectory 'restore-started'))) 'A command ran after recovery started.'
        $Payload | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $CaseDirectory 'arguments.json')
        $child = Start-HiddenChild @('-Grandchild', '-CaseDirectory', $CaseDirectory)
        $child.Dispose() # Intentionally leave a descendant alive after this root exits.
        exit 7
    }
    if ($WorkerCase) {
        . $taskWrapper -ProcessHelpersOnly
        $taskEvents = [Collections.Generic.List[string]]::new()
        $taskCommand = @($taskShell, '-NoLogo', '-NoProfile', '-File', $PSCommandPath,
            '-CommandChild', '-CaseDirectory', $CaseDirectory, '-Payload', '中文 value "quoted"')
        $taskExpectedFailure = $WorkerCase -in @('prepare-failure', 'non-executable')
        $taskExpectedMessage = if ($WorkerCase -eq 'non-executable') {
            $taskCommand = @('Write-Output', 'must not run inside the runner')
            'The desktop test command must be an executable; run scripts through pwsh -File.'
        } else { 'Expected preparation failure.' }
        $taskCaught = $false
        try {
            Invoke-QuietDesktopSession -Prepare {
                $taskEvents.Add('prepare')
                if ($taskExpectedFailure) { throw 'Expected preparation failure.' }
                Start-Sleep -Milliseconds 100
                Set-Content -LiteralPath (Join-Path $CaseDirectory 'prepare-finished') -Value complete
                $taskEvents.Add('prepared')
            } -TestCommand $taskCommand -Recover {
                if (-not $taskExpectedFailure) {
                    Assert-That (Test-Path -LiteralPath (Join-Path $CaseDirectory 'last-child-finished')) 'Recovery started while a test descendant was still alive.'
                }
                Set-Content -LiteralPath (Join-Path $CaseDirectory 'restore-started') -Value complete
                $taskEvents.Add('restore')
            }
        } catch {
            if (-not $taskExpectedFailure -or $_.Exception.Message -ne $taskExpectedMessage) { throw }
            $taskCaught = $true
        }
        if ($taskExpectedFailure) {
            Assert-That $taskCaught 'A preparation failure was ignored.'
            Assert-That (-not (Test-Path -LiteralPath (Join-Path $CaseDirectory 'arguments.json'))) 'A failed preparation still launched the command.'
            $taskExpectedEvents = if ($WorkerCase -eq 'non-executable') { 'restore' } else { 'prepare,restore' }
            Assert-That (($taskEvents -join ',') -eq $taskExpectedEvents) 'Preparation failure did not recover once.'
        } else {
            Assert-That ($LASTEXITCODE -eq 7) 'The command exit code was lost.'
            $taskReceived = Get-Content -LiteralPath (Join-Path $CaseDirectory 'arguments.json') -Raw | ConvertFrom-Json
            Assert-That ($taskReceived -eq '中文 value "quoted"') 'Command arguments were changed.'
            Assert-That (($taskEvents -join ',') -eq 'prepare,prepared,restore') 'The session phases were reordered.'
        }
        exit 0
    }

    $taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $taskTemporary = Join-Path $taskRepository ('out/test-results/quiet-process-' + [guid]::NewGuid().ToString('N'))
    try {
        . $taskWrapper -ProcessHelpersOnly
        # Options bind by full name only and stop at the command, so a command
        # may carry -Test, -Elevate or -Restore of its own.
        $parsed = ConvertFrom-QuietDesktopArguments @('-Elevate', 'pwsh', 'tools/build.ps1', '-Configuration', 'Release', '-Test', '-Restore', '-Elevate')
        Assert-That ($parsed.Elevate -and -not $parsed.Restore) 'Options after the command were taken by the runner.'
        Assert-That (($parsed.TestCommand -join '|') -eq 'pwsh|tools/build.ps1|-Configuration|Release|-Test|-Restore|-Elevate') 'The command arguments were changed.'
        $parsed = ConvertFrom-QuietDesktopArguments @('-statefile', 'x.json', '-WindowProcessId', '42', '--', '-dash-command', '中文 "q"')
        Assert-That ($parsed.StateFile -eq 'x.json' -and $parsed.WindowProcessId -eq 42) 'Option values were not read.'
        Assert-That (($parsed.TestCommand -join '|') -eq '-dash-command|中文 "q"') 'The -- separator did not start the command.'
        foreach ($rejected in @(@('-Test', 'pwsh'), @('-Elev', 'pwsh'), @('-StateFile'))) {
            $refused = $false
            try { [void](ConvertFrom-QuietDesktopArguments $rejected) } catch { $refused = $true }
            Assert-That $refused "Options were accepted by prefix or without a value: $($rejected -join ' ')."
        }
        # The same program name in two PATH directories runs the first one.
        $taskFirst = Join-Path $taskTemporary 'path-first'
        $taskSecond = Join-Path $taskTemporary 'path-second'
        foreach ($directory in @($taskFirst, $taskSecond)) {
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
            Set-Content -LiteralPath (Join-Path $directory 'quiet-desktop-probe.cmd') -Value "@exit /b 0"
        }
        $taskPath = $env:PATH
        try {
            $env:PATH = "$taskFirst;$taskSecond;$taskPath"
            $resolved = @(Resolve-QuietDesktopProgram 'quiet-desktop-probe')
            Assert-That ($resolved.Count -eq 1 -and $resolved[0] -eq (Join-Path $taskFirst 'quiet-desktop-probe.cmd')) "Duplicate programs were not resolved to the first: $($resolved -join '; ')."
        } finally { $env:PATH = $taskPath }

        foreach ($case in @('descendant-drain', 'prepare-failure', 'non-executable')) {
            $directory = Join-Path $taskTemporary $case
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
            $worker = Start-HiddenChild @('-WorkerCase', $case, '-CaseDirectory', $directory)
            try {
                $worker.WaitForExit()
                Assert-That ($worker.ExitCode -eq 0) "Lifecycle check failed: $case ($($worker.ExitCode))."
            } finally { $worker.Dispose() }
        }
        Write-Output 'PASS: options bind by full name before the command only; duplicate programs resolve to the first; preparation precedes command execution; recovery waits for all descendants; preparation failure blocks the command; exit code and Unicode arguments are preserved. No windows were created or changed.'
    } finally {
        $resolved = [IO.Path]::GetFullPath($taskTemporary)
        $allowed = [IO.Path]::GetFullPath((Join-Path $taskRepository 'out/test-results')) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected lifecycle fixture cleanup path.' }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }

    exit 0
}

if (-not $Probe -and -not $IsolatedWorker) {
    # No fixture windows exist before the outer runner has finished clearing
    # user applications. This one session owns their entire recovery lifetime.
    & $taskPwsh -NoProfile -File $taskWrapper -Elevate $taskPwsh -NoProfile -File $PSCommandPath -IsolatedWorker -OuterStateFile $taskOuterRecovery
    exit $LASTEXITCODE
}

if ($Probe) {
    if (-not $FixtureStateFile) { throw 'The probe requires its fixture-only recovery path.' }
    $taskRecovery = $FixtureStateFile
} else {
    if (-not $OuterStateFile -or -not (Test-Path -LiteralPath $OuterStateFile)) {
        throw 'The fixture worker must run inside an active outer desktop-isolation session.'
    }
    $taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        if (-not [Security.Principal.WindowsPrincipal]::new($taskIdentity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'The -Elevate session did not run its test command with administrator rights.'
        }
    } finally { $taskIdentity.Dispose() }
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class QuietDesktopFixture {
    delegate IntPtr WindowProc(IntPtr window, uint message, IntPtr first, IntPtr second);
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Placement {
        public int Length, Flags, ShowCommand;
        public Point MinPosition, MaxPosition;
        public Rect Normal;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct WindowClass {
        public uint Size, Style;
        public WindowProc Procedure;
        public int ClassExtra, WindowExtra;
        public IntPtr Instance, Icon, Cursor, Background;
        public string Menu, Name;
        public IntPtr SmallIcon;
    }
    [StructLayout(LayoutKind.Sequential)] struct Message {
        public IntPtr Window;
        public uint Id;
        public UIntPtr First;
        public IntPtr Second;
        public uint Time;
        public Point Position;
        public uint Private;
    }
    public sealed class Record {
        public string Name, Class;
        public long Handle;
        public uint Process, Thread;
        public bool Visible, Iconic, Topmost;
        public Placement Placement;
    }
    static readonly Dictionary<string, IntPtr> windows = new Dictionary<string, IntPtr>();
    static readonly WindowProc procedure = Dispatch;
    static readonly ManualResetEventSlim ready = new ManualResetEventSlim();
    static readonly ManualResetEventSlim dispatched = new ManualResetEventSlim();
    static Thread ui;
    static uint uiId;
    static Exception failure;
    static bool delayed;
    static volatile bool lateOrderArmed;
    static int lateOrderCount;
    static int helperMinimizations;
    static volatile bool revealHiddenArmed;
    static int appearingMinimizations;
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern ushort RegisterClassExW(ref WindowClass value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowExW(uint extended, string name, string title, uint style, int x, int y, int width, int height, IntPtr owner, IntPtr menu, IntPtr instance, IntPtr data);
    [DllImport("user32.dll")] static extern IntPtr DefWindowProcW(IntPtr window, uint message, IntPtr first, IntPtr second);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool EnableWindow(IntPtr window, bool enabled);
    [DllImport("user32.dll")] static extern int GetMessageW(out Message message, IntPtr window, uint first, uint last);
    [DllImport("user32.dll")] static extern bool TranslateMessage(ref Message message);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessageW(ref Message message);
    [DllImport("user32.dll")] static extern bool PostThreadMessageW(uint thread, uint message, IntPtr first, IntPtr second);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool GetWindowPlacement(IntPtr window, ref Placement placement);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtrW(IntPtr window, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern UIntPtr SetTimer(IntPtr window, UIntPtr id, uint milliseconds, IntPtr callback);
    [DllImport("user32.dll")] static extern bool KillTimer(IntPtr window, UIntPtr id);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("comctl32.dll")] static extern void InitCommonControls();

    static IntPtr Dispatch(IntPtr window, uint message, IntPtr first, IntPtr second) {
        if (message == 5 /* WM_SIZE */ && first.ToInt64() != 1 && lateOrderArmed &&
            windows.ContainsKey("slow") && windows["slow"] == window) {
            lateOrderArmed = false;
            SetTimer(window, new UIntPtr(1), 50, IntPtr.Zero);
        }
        if (message == 0x0113 /* WM_TIMER */ && first.ToInt64() == 1 &&
            windows.ContainsKey("slow") && windows["slow"] == window) {
            KillTimer(window, new UIntPtr(1));
            SetWindowPos(window, IntPtr.Zero, 0, 0, 0, 0, 0x0013 /* NOACTIVATE | NOMOVE | NOSIZE */);
            Interlocked.Increment(ref lateOrderCount);
        }
        if (message == 5 /* WM_SIZE */ && first.ToInt64() == 1 /* SIZE_MINIMIZED */) {
            if (windows.ContainsKey("appearing-hidden") && windows["appearing-hidden"] == window) {
                Interlocked.Increment(ref appearingMinimizations);
            } else if (windows.ContainsKey("event-helper") && windows["event-helper"] == window) {
                Interlocked.Increment(ref helperMinimizations);
            } else if (windows.ContainsKey("stubborn") && windows["stubborn"] == window) {
                ShowWindow(window, 9); // Exercise reversible hiding after a refused minimize.
            } else if (!delayed && windows.ContainsKey("slow") && windows["slow"] == window) {
                delayed = true;
                Thread.Sleep(700); // Longer than the old wrapper's fixed 500 ms delay.
            }
            if (revealHiddenArmed && windows.ContainsKey("owner") && windows["owner"] == window) {
                revealHiddenArmed = false;
                ShowWindow(windows["appearing-hidden"], 4);
            }
        }
        return DefWindowProcW(window, message, first, second);
    }

    static IntPtr Create(string name, uint extended = 0, IntPtr owner = default(IntPtr), uint style = 0x00CF0000, int width = 240, int height = 120) {
        var handle = CreateWindowExW(extended, "MuxoraQuietDesktopFixture", "Muxora desktop test: " + name, style,
            100 + windows.Count * 15, 100 + windows.Count * 15, width, height,
            owner, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
        if (handle == IntPtr.Zero) throw new InvalidOperationException("Native test window creation failed.");
        windows.Add(name, handle);
        return handle;
    }

    public static void Start() {
        ui = new Thread(() => {
            try {
                uiId = GetCurrentThreadId();
                var definition = new WindowClass { Size = (uint)Marshal.SizeOf<WindowClass>(), Procedure = procedure,
                    Name = "MuxoraQuietDesktopFixture", Instance = GetModuleHandleW(null) };
                if (RegisterClassExW(ref definition) == 0) throw new InvalidOperationException("Test class registration failed.");
                var owner = Create("owner"); ShowWindow(owner, 4);
                ShowWindow(Create("maximized"), 3);
                ShowWindow(Create("slow"), 4);
                ShowWindow(Create("stubborn"), 4);
                ShowWindow(Create("owned-tool", 0x88, owner, 0x80C00000), 4);
                ShowWindow(Create("event-helper", 0x08000080, default(IntPtr), 0x80000000, 13, 13), 4);
                Create("originally-hidden", 0x80, owner, 0x80C00000);
                Create("appearing-hidden");
                ShowWindow(Create("originally-minimized"), 7);
                EnableWindow(owner, false); // Owned modal/palette with its owner disabled.
                InitCommonControls();
                var tooltip = CreateWindowExW(0x80, "tooltips_class32", "", 0x80000000,
                    20, 20, 80, 30, IntPtr.Zero, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
                if (tooltip == IntPtr.Zero) throw new InvalidOperationException("Tooltip fixture creation failed.");
                windows.Add("tooltip", tooltip); ShowWindow(tooltip, 4);
                definition.Name = "Qt6120QWindowToolTipSaveBits";
                if (RegisterClassExW(ref definition) == 0) throw new InvalidOperationException("Tooltip class registration failed.");
                var qtTooltip = CreateWindowExW(0x08000080, definition.Name, "", 0x80000000,
                    30, 30, 80, 30, IntPtr.Zero, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
                if (qtTooltip == IntPtr.Zero) throw new InvalidOperationException("Qt-style tooltip fixture creation failed.");
                windows.Add("qt-tooltip", qtTooltip); ShowWindow(qtTooltip, 4);
                ready.Set();
                Message message;
                while (GetMessageW(out message, IntPtr.Zero, 0, 0) > 0) {
                    if (message.Window == IntPtr.Zero && message.Id == 0x8001) {
                        dispatched.Set();
                        continue;
                    }
                    TranslateMessage(ref message); DispatchMessageW(ref message);
                }
            } catch (Exception error) { failure = error; ready.Set(); }
            finally { foreach (var handle in windows.Values) DestroyWindow(handle); }
        });
        ui.IsBackground = true; ui.Start();
        if (!ready.Wait(5000)) throw new InvalidOperationException("Fixture did not start.");
        if (failure != null) throw failure;
        // Take the initial snapshot after the UI thread has dispatched the
        // creation/show messages queued before this barrier, including tooltips.
        if (!PostThreadMessageW(uiId, 0x8001, IntPtr.Zero, IntPtr.Zero) || !dispatched.Wait(5000))
            throw new InvalidOperationException("Fixture initialization messages did not complete.");
    }

    public static Record Inspect(long handle, string name) {
        var window = new IntPtr(handle);
        var record = new Record { Handle = handle, Name = name, Visible = IsWindowVisible(window), Iconic = IsIconic(window) };
        record.Topmost = (GetWindowLongPtrW(window, -20).ToInt64() & 8) != 0;
        record.Thread = GetWindowThreadProcessId(window, out record.Process);
        var cls = new StringBuilder(256); GetClassNameW(window, cls, cls.Capacity); record.Class = cls.ToString();
        var placement = new Placement { Length = Marshal.SizeOf<Placement>() };
        GetWindowPlacement(window, ref placement); record.Placement = placement;
        return record;
    }

    public static Record[] Snapshot() {
        var result = new List<Record>();
        foreach (var entry in windows) result.Add(Inspect(entry.Value.ToInt64(), entry.Key));
        return result.ToArray();
    }

    public static void ArmLateOrder() { lateOrderArmed = true; }
    public static int LateOrderCount() { return Volatile.Read(ref lateOrderCount); }
    public static int HelperMinimizations() { return Volatile.Read(ref helperMinimizations); }
    public static void ArmHiddenAppearance() { revealHiddenArmed = true; }
    public static int AppearingMinimizations() { return Volatile.Read(ref appearingMinimizations); }

    public static long[] Order(long[] handles) {
        var remaining = new HashSet<long>(handles); var result = new List<long>();
        EnumWindows((window, parameter) => {
            if (remaining.Contains(window.ToInt64())) result.Add(window.ToInt64()); return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }

    public static void Stop() {
        PostThreadMessageW(uiId, 18 /* WM_QUIT */, IntPtr.Zero, IntPtr.Zero);
        if (!ui.Join(5000)) throw new InvalidOperationException("Fixture did not stop.");
        foreach (var handle in windows.Values)
            if (IsWindow(handle)) throw new InvalidOperationException("A fixture window survived its UI thread cleanup.");
    }
}
'@

function Assert-That([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

if ($Probe) {
    $original = @(Get-Content -LiteralPath (Join-Path $CaseDirectory 'fixture.json') -Raw | ConvertFrom-Json)
    foreach ($entry in $original) {
        $current = [QuietDesktopFixture]::Inspect($entry.Handle, $entry.Name)
        if ($entry.Name -in @('originally-hidden', 'appearing-hidden')) {
            Assert-That (-not $current.Visible) 'An originally hidden window was shown.'
        } elseif ($entry.Name -eq 'originally-minimized') {
            Assert-That $current.Iconic 'An originally minimized window was restored during testing.'
        } elseif ($entry.Name -in @('tooltip', 'qt-tooltip')) {
            Assert-That ($current.Visible -eq $entry.Visible) 'A tooltip visibility state was unnecessarily changed.'
        } else {
            Assert-That (-not $current.Visible -or $current.Iconic) "The test command started before $($entry.Name) cleared."
        }
    }
    $snapshot = Get-Content -LiteralPath $taskRecovery -Raw | ConvertFrom-Json
    Assert-That ($snapshot.version -eq 2) 'The recovery snapshot is not versioned.'
    foreach ($entry in $snapshot.windows) {
        Assert-That ($entry.Process -eq $original[0].Process) 'An inner recovery snapshot captured a user application.'
    }
    foreach ($tooltip in $original | Where-Object { $_.Name -in @('tooltip', 'qt-tooltip') }) {
        Assert-That (-not ($snapshot.windows | Where-Object Handle -eq $tooltip.Handle)) 'A tooltip entered the recovery snapshot.'
    }
    foreach ($untouched in $original | Where-Object { $_.Name -in @('originally-hidden', 'appearing-hidden', 'originally-minimized') }) {
        Assert-That (-not ($snapshot.windows | Where-Object Handle -eq $untouched.Handle)) 'An originally hidden or minimized window entered the recovery snapshot.'
    }
    foreach ($entry in $snapshot.windows) {
        $current = [QuietDesktopFixture]::Inspect($entry.Handle, 'application')
        if ($current.Process -eq $entry.Process -and $current.Thread -eq $entry.Thread -and $current.Class -eq $entry.Class) {
            Assert-That (-not $current.Visible -or $current.Iconic) 'Another recorded application window remains visible.'
        }
    }
    Set-Content -LiteralPath (Join-Path $CaseDirectory 'probe-ran') -Value $ProbeExitCode
    exit $ProbeExitCode
}

$taskOuterSnapshot = Get-Content -LiteralPath $OuterStateFile -Raw | ConvertFrom-Json
Assert-That ($taskOuterSnapshot.version -eq 2) 'The outer isolation session did not save window identities.'
foreach ($entry in $taskOuterSnapshot.windows) {
    $current = [QuietDesktopFixture]::Inspect($entry.Handle, 'outer application')
    if ($current.Process -eq $entry.Process -and $current.Thread -eq $entry.Thread -and $current.Class -eq $entry.Class) {
        Assert-That (-not $current.Visible -or $current.Iconic) 'Fixture creation was attempted before an outer application cleared.'
    }
}
$taskTemporary = Join-Path $taskRepository ('out/test-results/quiet-desktop-fixture-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskTemporary -Force | Out-Null
$taskRecovery = Join-Path $taskTemporary 'fixture-windows.json'
$taskFixtureStarted = $false
try {
    $taskFixtureStarted = $true
    [QuietDesktopFixture]::Start()
    $original = @([QuietDesktopFixture]::Snapshot())
    $original | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskTemporary 'fixture.json') -Encoding utf8
    $orderedHandles = @($original | Where-Object { $_.Name -notin @('tooltip', 'qt-tooltip', 'originally-hidden', 'appearing-hidden', 'originally-minimized') } | ForEach-Object Handle)
    $originalOrder = [QuietDesktopFixture]::Order([long[]]$orderedHandles)
    foreach ($expectedExit in @(0, 7)) {
        $lateOrderBefore = [QuietDesktopFixture]::LateOrderCount()
        [QuietDesktopFixture]::ArmLateOrder()
        & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID $taskPwsh -NoProfile -File $PSCommandPath -Probe -CaseDirectory $taskTemporary -FixtureStateFile $taskRecovery -ProbeExitCode $expectedExit
        Assert-That ($LASTEXITCODE -eq $expectedExit) "The wrapper changed the child exit code: $LASTEXITCODE."
        Assert-That (Test-Path -LiteralPath (Join-Path $taskTemporary 'probe-ran')) 'The isolated command did not execute.'
        Assert-That (-not (Test-Path -LiteralPath $taskRecovery)) 'Successful recovery left its snapshot behind.'
        foreach ($entry in $original) {
            $current = [QuietDesktopFixture]::Inspect($entry.Handle, $entry.Name)
            Assert-That ($current.Visible -eq $entry.Visible -and $current.Iconic -eq $entry.Iconic) "Visibility was not restored for $($entry.Name)."
            Assert-That ($current.Placement.ShowCommand -eq $entry.Placement.ShowCommand) "Window state was not restored for $($entry.Name)."
            Assert-That ($current.Placement.Normal.Equals($entry.Placement.Normal)) "Normal geometry changed for $($entry.Name)."
            Assert-That ($current.Topmost -eq $entry.Topmost) "The topmost band changed for $($entry.Name)."
        }
        $elapsed = [Diagnostics.Stopwatch]::StartNew()
        do {
            $restoredOrder = [QuietDesktopFixture]::Order([long[]]$orderedHandles)
            if (($restoredOrder -join ',') -eq ($originalOrder -join ',')) { break }
            Start-Sleep -Milliseconds 25
        } while ($elapsed.ElapsedMilliseconds -lt 3000)
        Assert-That (($restoredOrder -join ',') -eq ($originalOrder -join ',')) "Application Z order was not restored: expected $($originalOrder -join ','), got $($restoredOrder -join ',')."
        Assert-That ([QuietDesktopFixture]::LateOrderCount() -gt $lateOrderBefore) 'The delayed restore-time Z-order change did not execute.'
        Assert-That ([QuietDesktopFixture]::HelperMinimizations() -eq 0) 'A non-activating helper without minimize controls was unnecessarily minimized.'
    }
    # The child's -Command must remain an argument, not bind to the wrapper.
    $taskChildCommand = "& '$($PSCommandPath.Replace("'", "''"))' -Probe -CaseDirectory '$($taskTemporary.Replace("'", "''"))' -FixtureStateFile '$($taskRecovery.Replace("'", "''"))' -ProbeExitCode 9" + '; exit $LASTEXITCODE'
    & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID $taskPwsh -NoProfile -Command $taskChildCommand
    Assert-That ($LASTEXITCODE -eq 9) 'The child PowerShell -Command argument was not forwarded intact.'
    Assert-That ((Get-Content -LiteralPath (Join-Path $taskTemporary 'probe-ran') -Raw).Trim() -eq '9') 'The child PowerShell command did not execute.'
    # Unsupported snapshots must not perform window actions or be deleted.
    $owner = $original | Where-Object Name -eq owner
    [void][QuietDesktopFixture]::ShowWindow([IntPtr]$owner.Handle, 7)
    @{ foreground = 0; windows = @($owner.Handle) } | ConvertTo-Json |
        Set-Content -LiteralPath $taskRecovery -Encoding utf8
    & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID -Restore
    Assert-That ($LASTEXITCODE -ne 0) 'An unsupported recovery snapshot was accepted.'
    Assert-That (Test-Path -LiteralPath $taskRecovery) 'Rejected recovery deleted its snapshot.'
    Assert-That ([QuietDesktopFixture]::IsIconic([IntPtr]$owner.Handle)) 'Rejected recovery changed a window.'
    # An HWND reused by another process must never receive recovery actions.
    [void][QuietDesktopFixture]::ShowWindow([IntPtr]$owner.Handle, 7)
    $forged = $owner | ConvertTo-Json -Depth 6 | ConvertFrom-Json
    $forged.Process = [long]$forged.Process + 1
    $foreground = $forged | ConvertTo-Json -Depth 6 | ConvertFrom-Json
    $foreground.Handle = 0
    @{ version = 2; foreground = 0; foregroundWindow = $foreground; windows = @($forged) } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskRecovery -Encoding utf8
    & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID -Restore
    Assert-That ($LASTEXITCODE -eq 0) 'Recovery of a stale window identity failed.'
    Assert-That ([QuietDesktopFixture]::IsIconic([IntPtr]$owner.Handle)) 'Recovery changed a window with a different process identity.'
    # A vanished predecessor must not prevent a surviving peer from restoring.
    @{ version = 2; foreground = 0; foregroundWindow = $foreground; windows = @($forged, $owner) } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskRecovery -Encoding utf8
    & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID -Restore
    Assert-That ($LASTEXITCODE -eq 0) 'Recovery failed after a predecessor identity disappeared.'
    Assert-That (-not [QuietDesktopFixture]::IsIconic([IntPtr]$owner.Handle)) 'Recovery left the surviving peer minimized.'
    [void][QuietDesktopFixture]::ShowWindow([IntPtr]$owner.Handle, 9)
    # An initially hidden window that appears during cleanup remains untouched;
    # it blocks testing instead of joining the recovery snapshot.
    $unexpectedCommand = Join-Path $taskTemporary 'unexpected-command-ran'
    $taskBlockedCommand = "Set-Content -LiteralPath '$($unexpectedCommand.Replace("'", "''"))' -Value unexpected"
    [QuietDesktopFixture]::ArmHiddenAppearance()
    & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID $taskPwsh -NoProfile -Command $taskBlockedCommand
    Assert-That ($LASTEXITCODE -ne 0) 'A newly visible window did not block the test command.'
    Assert-That (-not (Test-Path -LiteralPath $unexpectedCommand)) 'The test command ran under a newly visible application window.'
    $appearing = $original | Where-Object Name -eq 'appearing-hidden'
    $currentAppearing = [QuietDesktopFixture]::Inspect($appearing.Handle, $appearing.Name)
    Assert-That ($currentAppearing.Visible -and -not $currentAppearing.Iconic) 'Cleanup changed a window that was hidden at the initial snapshot.'
    Assert-That ([QuietDesktopFixture]::AppearingMinimizations() -eq 0) 'A newly visible window was added to cleanup.'
    Assert-That (-not (Test-Path -LiteralPath $taskRecovery)) 'Recovery of the initial windows did not finish after cleanup was blocked.'
    [void][QuietDesktopFixture]::ShowWindow([IntPtr]$appearing.Handle, 0)
} finally {
    # Destroy every fixture before the worker returns to the outer runner.
    # A failed inner snapshot then contains only dead fixture identities.
    if ($taskFixtureStarted) { [QuietDesktopFixture]::Stop() }
    if (Test-Path -LiteralPath $taskRecovery) {
        & $taskPwsh -NoProfile -File $taskWrapper -StateFile $taskRecovery -WindowProcessId $PID -Restore
        Assert-That ($LASTEXITCODE -eq 0) 'Fixture-only recovery cleanup failed.'
    }
    $taskResolved = [IO.Path]::GetFullPath($taskTemporary)
    $taskAllowed = [IO.Path]::GetFullPath((Join-Path $taskRepository 'out/test-results')) + [IO.Path]::DirectorySeparatorChar
    if (-not $taskResolved.StartsWith($taskAllowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected fixture cleanup path.' }
    Remove-Item -LiteralPath $taskResolved -Recurse -Force
}
Write-Output 'PASS: the -Elevate session runs the command with administrator rights; owned/modal/tool windows, delayed and refused minimization, unchanged hidden/minimized windows and tooltips, geometry/Z-order recovery with delayed reordering, child failure and -Command forwarding, unsupported snapshot rejection, HWND identity and vanished-predecessor guards, a frozen initial snapshot, and fixture cleanup inside one outer desktop-isolation session.'
