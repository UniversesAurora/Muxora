<#
.SYNOPSIS
Reports the native frame geometry of running Muxora windows, read-only.

.DESCRIPTION
For each visible Muxora top-level window: placement state, window and client
rectangles in physical pixels, the monitor and its work area, the monitor DPI,
and for the four work-area corner pixels the WM_NCHITTEST result and the
window that actually lies under the pixel. Nothing is clicked, moved or
changed. Use it to compare machines with different scaling when a pixel at
the screen edge does not reach the control drawn there.

.EXAMPLE
pwsh tools/diagnostics/window-frame-probe.ps1
#>
[CmdletBinding()]
param([string]$ProcessName = 'muxora')
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class FrameProbe {
    public struct Rect { public int Left, Top, Right, Bottom; }
    public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    public struct MonitorInfo { public int Size; public Rect Monitor; public Rect Work; public uint Flags; }
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr monitor, int type, out uint x, out uint y);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr window, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);

    public static string ClassName(IntPtr window) {
        var name = new StringBuilder(256); GetClassNameW(window, name, name.Capacity); return name.ToString();
    }
    public static uint ProcessId(IntPtr window) { uint process; GetWindowThreadProcessId(window, out process); return process; }
    public static List<long> TopWindows(uint process) {
        var result = new List<long>();
        EnumWindows((window, parameter) => {
            if (IsWindowVisible(window) && GetWindow(window, 4) == IntPtr.Zero && ProcessId(window) == process
                && ClassName(window).StartsWith("Qt")) result.Add(window.ToInt64());
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static int ChildCount(IntPtr window) {
        int count = 0; EnumChildWindows(window, (child, parameter) => { ++count; return true; }, IntPtr.Zero); return count;
    }
}
'@

# Physical pixels for every monitor, whatever this console's own awareness is.
[void][FrameProbe]::SetProcessDpiAwarenessContext([IntPtr]-4) # PER_MONITOR_AWARE_V2

function Format-Rect($rect) { '({0},{1})-({2},{3}) {4}x{5}' -f $rect.Left, $rect.Top, $rect.Right, $rect.Bottom, ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top) }
function Get-HitName([long]$hit) {
    $names = @{ 1 = 'HTCLIENT'; 2 = 'HTCAPTION'; 8 = 'HTMINBUTTON'; 9 = 'HTMAXBUTTON'; 20 = 'HTCLOSE'; 10 = 'HTLEFT'; 11 = 'HTRIGHT'
        12 = 'HTTOP'; 13 = 'HTTOPLEFT'; 14 = 'HTTOPRIGHT'; 15 = 'HTBOTTOM'; 16 = 'HTBOTTOMLEFT'; 17 = 'HTBOTTOMRIGHT'; 0 = 'HTNOWHERE' }
    if ($names.ContainsKey([int]$hit)) { $names[[int]$hit] } else { "$hit" }
}

$processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if (-not $processes) { "No running $ProcessName process."; return }
foreach ($process in $processes) {
    "process $($process.Id) $($process.Path)"
    foreach ($value in [FrameProbe]::TopWindows([uint32]$process.Id)) {
        $window = [IntPtr]$value
        $outer = New-Object FrameProbe+Rect; [void][FrameProbe]::GetWindowRect($window, [ref]$outer)
        $client = New-Object FrameProbe+Rect; [void][FrameProbe]::GetClientRect($window, [ref]$client)
        $origin = New-Object FrameProbe+Point; [void][FrameProbe]::ClientToScreen($window, [ref]$origin)
        $monitor = [FrameProbe]::MonitorFromWindow($window, 2)
        $info = New-Object FrameProbe+MonitorInfo; $info.Size = [Runtime.InteropServices.Marshal]::SizeOf($info)
        [void][FrameProbe]::GetMonitorInfoW($monitor, [ref]$info)
        $dpiX = 0; $dpiY = 0; [void][FrameProbe]::GetDpiForMonitor($monitor, 0, [ref]$dpiX, [ref]$dpiY)
        ''
        'window 0x{0:x} class={1} maximized={2} nativeChildren={3}' -f $value, [FrameProbe]::ClassName($window), [FrameProbe]::IsZoomed($window), [FrameProbe]::ChildCount($window)
        '  outer   {0}' -f (Format-Rect $outer)
        '  client  origin ({0},{1}) size {2}x{3}' -f $origin.X, $origin.Y, $client.Right, $client.Bottom
        '  monitor {0}' -f (Format-Rect $info.Monitor)
        '  work    {0}' -f (Format-Rect $info.Work)
        '  dpi     monitor {0} ({1}%) window {2}' -f $dpiX, [math]::Round($dpiX / 96 * 100), [FrameProbe]::GetDpiForWindow($window)
        $corners = [ordered]@{
            'top-left' = @($info.Work.Left, $info.Work.Top); 'top-right' = @(($info.Work.Right - 1), $info.Work.Top)
            'bottom-left' = @($info.Work.Left, ($info.Work.Bottom - 1)); 'bottom-right' = @(($info.Work.Right - 1), ($info.Work.Bottom - 1))
        }
        foreach ($corner in $corners.GetEnumerator()) {
            $x = $corner.Value[0]; $y = $corner.Value[1]
            $lParam = [IntPtr]((($y -band 0xffff) -shl 16) -bor ($x -band 0xffff))
            $hit = [IntPtr]::Zero
            [void][FrameProbe]::SendMessageTimeoutW($window, 0x84, [IntPtr]::Zero, $lParam, 2, 1000, [ref]$hit) # WM_NCHITTEST
            $point = New-Object FrameProbe+Point; $point.X = $x; $point.Y = $y
            $under = [FrameProbe]::WindowFromPoint($point)
            $root = [FrameProbe]::GetAncestor($under, 2)
            $inClient = $x -ge $origin.X -and $x -lt $origin.X + $client.Right -and $y -ge $origin.Y -and $y -lt $origin.Y + $client.Bottom
            '  {0,-12} ({1},{2}) hit={3} inClient={4} underPixel=0x{5:x} {6} pid {7}{8}' -f $corner.Key, $x, $y, (Get-HitName $hit.ToInt64()),
                $inClient, $under.ToInt64(), [FrameProbe]::ClassName($under), [FrameProbe]::ProcessId($under),
                $(if ($root -eq $window) { ' (this window)' } else { ' (another window)' })
        }
    }
}
