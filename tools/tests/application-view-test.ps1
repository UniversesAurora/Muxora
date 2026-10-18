[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepository 'tools/deployment.ps1')
$taskOutput = Join-Path $taskRepository 'out'
$taskFixture = Join-Path $taskOutput 'application-view-checks/current'
$taskState = Join-Path $taskFixture '显示与全屏 state'
$taskStateFile = Join-Path $taskState 'window-layout.json'
$taskReport = Join-Path $taskOutput 'test-results/application-view.json'
$taskExecutable = Join-Path ([IO.Path]::GetFullPath($PackageDirectory, $taskRepository)) 'muxora.exe'
if (!(Test-Path -LiteralPath $taskExecutable -PathType Leaf)) { throw 'Deployed executable is missing.' }
$taskOriginalEnvironment = @{}
foreach ($taskName in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    $taskOriginalEnvironment[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
}
$taskProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$taskRuns = [Collections.Generic.List[object]]::new()

function Read-ViewSnapshot {
    if (!(Test-Path -LiteralPath $taskStateFile -PathType Leaf)) { throw 'Application did not save a view snapshot.' }
    Get-Content -LiteralPath $taskStateFile -Raw | ConvertFrom-Json -AsHashtable
}
function Write-ViewSnapshot($Snapshot) {
    [IO.File]::WriteAllText($taskStateFile, (ConvertTo-Json -InputObject $Snapshot -Depth 100))
}
function Copy-ViewSnapshot($Snapshot) {
    ConvertTo-Json -InputObject $Snapshot -Depth 100 | ConvertFrom-Json -AsHashtable
}
function Get-ViewWindow($Snapshot) {
    if ($Snapshot.windows.Count -ne 1 -or $Snapshot.model.windows.Count -ne 1) { throw 'View fixture must contain one window.' }
    $Snapshot.windows[0]
}
function Get-ViewPreferences($Window) {
    $taskPreferences = [ordered]@{}
    foreach ($taskField in @('toolbarsVisible', 'statusBarVisible', 'tabBarsVisible', 'sideBarsVisible')) {
        if (!$Window.Contains($taskField) -or $Window[$taskField] -isnot [bool]) { throw "Missing view preference: $taskField" }
        $taskPreferences[$taskField] = $Window[$taskField]
    }
    $taskPreferences
}
function Assert-ViewSnapshot($Snapshot, [string]$Mode, [bool]$Zen, $Preferences, $NormalWindow) {
    $taskWindow = Get-ViewWindow $Snapshot
    if ($taskWindow.Contains('viewState')) { throw 'A temporary fullscreen/Zen state was persisted.' }
    # The isolated Lua command checks the live runtime before writing this run's
    # completion marker. The disk snapshot deliberately has no view-state field.
    if ($taskRuns[-1].mode -ne $Mode -or $taskRuns[-1].zen -ne $Zen) { throw "Unexpected verified runtime scope: $Mode / Zen=$Zen" }
    foreach ($taskField in $Preferences.Keys) {
        if ($taskWindow[$taskField] -ne $Preferences[$taskField]) { throw "View scope changed a saved preference: $taskField" }
    }
    foreach ($taskField in @('x', 'y', 'width', 'height', 'maximized')) {
        if ($taskWindow[$taskField] -ne $NormalWindow[$taskField]) { throw "View scope changed normal placement: $taskField" }
    }
    if ((ConvertTo-Json -InputObject $Snapshot.model -Depth 100 -Compress) -ne $script:taskExpectedModel) { throw 'View scope changed the workspace model.' }
    if ((@($Snapshot.panes.id | Sort-Object) -join '|') -ne $script:taskExpectedPanes) { throw 'View scope changed the content IDs.' }
    $taskSequence = @(
        foreach ($taskField in @('rows', 'bottomRows')) {
            foreach ($taskRow in $taskWindow.strips[$taskField]) { foreach ($taskStrip in $taskRow) { $taskStrip } }
        }
    )
    if (@($taskSequence | Where-Object { $_ -eq 'workspace' }).Count -ne 1 -or
        (ConvertTo-Json -InputObject $taskWindow.strips.bottomRows[0] -Compress) -ne '["workspace"]') {
        throw 'Bottom toolbar lost its stable ID or saved order.'
    }
}
function Invoke-ViewRun([string]$Name, [string[]]$Command = @(), [string]$Mode = 'normal', [bool]$Zen = $false, [switch]$Fresh) {
    $taskLuaMode = switch ($Mode) {
        'normal' { 'normal' }
        'windowFullscreen' { 'window' }
        'groupFullscreen' { 'group' }
        'paneFullscreen' { 'pane' }
        default { throw "Unknown expected runtime mode: $Mode" }
    }
    $taskLuaCommands = foreach ($taskId in $Command) {
        if ($taskId -eq 'window.fullscreen.exit') {
            "  assert(muxora.windows.mode(context.window)~='normal')"
        }
        "  assert(muxora.commands.execute('$taskId'))"
    }
    $taskMarker = "view-verified:$Name"
    $taskLuaZen = if ($Zen) { 'true' } else { 'false' }
    $taskUser = Join-Path $taskState 'User'
    New-Item -ItemType Directory -Path $taskUser -Force | Out-Null
    @"
muxora.commands.register({id='user.view_check',title='View state check'}, function(context)
$(($taskLuaCommands -join "`n"))
  assert(muxora.windows.mode(context.window)=='$taskLuaMode')
  assert(muxora.windows.zen(context.window)==$taskLuaZen)
  assert(muxora.windows.set_title(context.window,'$taskMarker'))
  assert(muxora.commands.execute('layout.save'))
  assert(muxora.commands.execute('application.exit'))
end)
"@ | Set-Content -LiteralPath (Join-Path $taskUser 'init.lua') -Encoding utf8NoBOM
    $taskArguments = @('--smoke', '--state-dir', ('"' + $taskState + '"'))
    if ($Fresh) { $taskArguments += '--fresh' }
    $taskArguments += @('--command', 'user.view_check')
    $taskProcess = Start-Process -FilePath $taskExecutable -ArgumentList $taskArguments `
        -WorkingDirectory (Split-Path -Parent $taskExecutable) -WindowStyle Hidden -PassThru
    $taskProcesses.Add($taskProcess)
    if (!$taskProcess.WaitForExit(15000)) {
        $null = $taskProcess.CloseMainWindow()
        $null = $taskProcess.WaitForExit(5000)
        throw "Application did not finish: $Name"
    }
    $taskProcess.Refresh()
    if ($taskProcess.ExitCode -ne 0) { throw "Application view check failed: $Name ($($taskProcess.ExitCode))" }
    $taskSnapshot = Read-ViewSnapshot
    $taskWindow = Get-ViewWindow $taskSnapshot
    if ($taskWindow.title -ne $taskMarker) { throw "Live fullscreen/Zen assertions did not complete: $Name" }
    if ($taskWindow.Contains('viewState')) { throw "Temporary view state was saved: $Name" }
    $taskRuns.Add(@{ name = $Name; command = $Command; exitCode = $taskProcess.ExitCode;
        mode = $Mode; zen = $Zen; runtimeAssertedByLua = $true;
        maximized = $taskWindow.maximized; preferences = Get-ViewPreferences $taskWindow })
    $taskSnapshot
}

try {
    Remove-OutputItem $taskFixture $taskOutput
    Remove-OutputItem $taskReport $taskOutput
    New-Item -ItemType Directory -Path $taskState -Force | Out-Null
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $taskInitial = Invoke-ViewRun 'initial-window' -Fresh
    $taskInitialWindow = Get-ViewWindow $taskInitial
    $taskPreferences = Get-ViewPreferences $taskInitialWindow
    foreach ($taskField in $taskPreferences.Keys) {
        if (!$taskPreferences[$taskField]) { throw "Fresh window unexpectedly hides $taskField" }
    }
    $taskHidden = Invoke-ViewRun 'hide-toolbar-in-normal-mode' 'window.toolbars.toggle'
    if ((Get-ViewWindow $taskHidden).toolbarsVisible) { throw 'Toolbar visibility command did not hide the toolbar.' }
    $taskHidden = Invoke-ViewRun 'hide-status-in-normal-mode' 'window.statusBar.toggle'
    $taskNormalWindow = Get-ViewWindow $taskHidden
    $taskPreferences['toolbarsVisible'] = $false; $taskPreferences['statusBarVisible'] = $false
    foreach ($taskField in $taskPreferences.Keys) {
        if ($taskNormalWindow[$taskField] -ne $taskPreferences[$taskField]) { throw "Normal visibility changed another bar: $taskField" }
    }
    if ($taskNormalWindow.Contains('viewState')) { throw 'Visibility command persisted a runtime mode.' }
    $script:taskExpectedModel = ConvertTo-Json -InputObject $taskHidden.model -Depth 100 -Compress
    $script:taskExpectedPanes = @($taskHidden.panes.id | Sort-Object) -join '|'
    # Change this isolated fixture only. Retain the strip ID, its settings and
    # all other toolbar positions; restart must not duplicate or drop the strip.
    $taskNormalWindow.strips['rows'] = @(
        foreach ($taskRow in $taskNormalWindow.strips.rows) { ,@($taskRow | Where-Object { $_ -ne 'workspace' }) }
    )
    $taskNormalWindow.strips['bottomRows'] = @(,@('workspace'))
    Write-ViewSnapshot $taskHidden
    $taskBaseline = Invoke-ViewRun 'restore-bottom-toolbar-fixture'
    $taskNormalWindow = Get-ViewWindow $taskBaseline
    Assert-ViewSnapshot $taskBaseline 'normal' $false $taskPreferences $taskNormalWindow
    $taskScopes = @(
        @{ mode = 'groupFullscreen'; command = 'window.fullscreen.group' },
        @{ mode = 'paneFullscreen'; command = 'window.fullscreen.pane' },
        @{ mode = 'windowFullscreen'; command = 'window.fullscreen.toggle' }
    )
    foreach ($taskMaximized in @($false, $true)) {
        foreach ($taskScope in $taskScopes) {
            $taskFixtureSnapshot = Copy-ViewSnapshot $taskBaseline
            $taskDesiredWindow = Get-ViewWindow $taskFixtureSnapshot
            $taskDesiredWindow['maximized'] = $taskMaximized
            Write-ViewSnapshot $taskFixtureSnapshot
            $taskLabel = "$($taskScope.mode)-maximized-$taskMaximized"
            $taskSnapshot = Invoke-ViewRun "enter-$taskLabel" $taskScope.command -Mode $taskScope.mode
            Assert-ViewSnapshot $taskSnapshot $taskScope.mode $false $taskPreferences $taskDesiredWindow
            $taskSnapshot = Invoke-ViewRun "restart-$taskLabel"
            Assert-ViewSnapshot $taskSnapshot 'normal' $false $taskPreferences $taskDesiredWindow
            $taskSnapshot = Invoke-ViewRun "exit-$taskLabel" @($taskScope.command, 'window.fullscreen.exit')
            Assert-ViewSnapshot $taskSnapshot 'normal' $false $taskPreferences $taskDesiredWindow
        }
    }
    Write-ViewSnapshot $taskBaseline
    $taskSnapshot = Invoke-ViewRun 'enter-window-for-zen' 'window.fullscreen.toggle' -Mode 'windowFullscreen'
    Assert-ViewSnapshot $taskSnapshot 'windowFullscreen' $false $taskPreferences $taskNormalWindow
    $taskSnapshot = Invoke-ViewRun 'enable-zen' 'window.zen.toggle' -Mode 'windowFullscreen' -Zen $true
    Assert-ViewSnapshot $taskSnapshot 'windowFullscreen' $true $taskPreferences $taskNormalWindow
    $taskSnapshot = Invoke-ViewRun 'restart-zen'
    Assert-ViewSnapshot $taskSnapshot 'normal' $false $taskPreferences $taskNormalWindow
    $taskSnapshot = Invoke-ViewRun 'show-toolbar-exits-zen' @('window.zen.toggle', 'window.toolbars.toggle') -Mode 'windowFullscreen'
    $taskRestoredPreferences = [ordered]@{} + $taskPreferences
    $taskRestoredPreferences['toolbarsVisible'] = $true
    Assert-ViewSnapshot $taskSnapshot 'windowFullscreen' $false $taskRestoredPreferences $taskNormalWindow
    $taskSnapshot = Invoke-ViewRun 'exit-fullscreen-after-zen' @('window.fullscreen.toggle', 'window.fullscreen.exit')
    Assert-ViewSnapshot $taskSnapshot 'normal' $false $taskRestoredPreferences $taskNormalWindow
    $taskSnapshot = Invoke-ViewRun 'restart-normal-after-zen'
    Assert-ViewSnapshot $taskSnapshot 'normal' $false $taskRestoredPreferences $taskNormalWindow
} catch {
    @{ error = $_.Exception.Message; runs = $taskRuns.ToArray();
       expectedModel = $script:taskExpectedModel | ConvertFrom-Json -AsHashtable;
       snapshot = Read-ViewSnapshot } | ConvertTo-Json -Depth 100 |
        Set-Content -LiteralPath (Join-Path $taskOutput 'test-results/application-view-failure.json') -Encoding utf8NoBOM
    throw
} finally {
    foreach ($taskName in $taskOriginalEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskName, $taskOriginalEnvironment[$taskName], 'Process')
    }
    $taskAllStopped = $true
    foreach ($taskProcess in $taskProcesses) {
        if (!$taskProcess.HasExited) {
            $null = $taskProcess.CloseMainWindow()
            if (!$taskProcess.WaitForExit(5000)) { $taskAllStopped = $false }
        }
        $taskProcess.Dispose()
    }
    if ($taskAllStopped) { Remove-OutputItem $taskFixture $taskOutput }
    else { throw 'An isolated view-check process did not close; its fixture was preserved.' }
}
if (Test-Path -LiteralPath $taskFixture) { throw 'Application view fixture was not cleaned.' }
New-Item -ItemType Directory -Path (Split-Path -Parent $taskReport) -Force | Out-Null
@{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); sdkRemovedFromPath = $true;
    temporaryStateCleanedOnExit = $true; transientViewStateNotPersisted = $true; liveModesAssertedByLua = $true;
    modelAndContentIdsPreserved = $true; bottomToolbarStableId = 'workspace';
    normalAndMaximizedPlacementRestored = $true; runs = $taskRuns.ToArray() } |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $taskReport -Encoding utf8NoBOM
Write-Host "Application view checks passed: $($taskRuns.Count) launches, fullscreen scopes, Zen and bottom toolbar restart."
