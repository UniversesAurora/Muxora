[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepository 'tools/deployment.ps1')
$taskOutput = Join-Path $taskRepository 'out'
$taskFixture = Join-Path $taskOutput 'application-exit-checks/current'
$taskState = Join-Path $taskFixture '退出与恢复 state'
$taskExecutable = Join-Path ([IO.Path]::GetFullPath($PackageDirectory, $taskRepository)) 'muxora.exe'
if (!(Test-Path -LiteralPath $taskExecutable -PathType Leaf)) { throw 'Deployed executable is missing.' }
$taskOriginalPath = $env:PATH
$taskOriginalPluginPath = $env:QT_PLUGIN_PATH
$taskOriginalPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$taskRuns = [Collections.Generic.List[object]]::new()
function Invoke-ApplicationExitCheck([string]$Name, [string[]]$Arguments, [int]$ExpectedExit = 0) {
    $taskProcess = Start-Process -FilePath $taskExecutable -ArgumentList $Arguments `
        -WorkingDirectory (Split-Path -Parent $taskExecutable) -WindowStyle Hidden -PassThru
    if (!$taskProcess.WaitForExit(15000)) {
        $null = $taskProcess.CloseMainWindow()
        throw "Application did not finish: $Name"
    }
    $taskProcess.Refresh()
    if ($taskProcess.ExitCode -ne $ExpectedExit) { throw "Application check failed: $Name ($($taskProcess.ExitCode))" }
    $taskRuns.Add(@{ name = $Name; exitCode = $taskProcess.ExitCode })
    $taskProcess.Dispose()
}
function Read-ExitSnapshot {
    Get-Content -LiteralPath (Join-Path $taskState 'window-layout.json') -Raw | ConvertFrom-Json
}
try {
    Remove-OutputItem $taskFixture $taskOutput
    New-Item -ItemType Directory -Path $taskFixture -Force | Out-Null
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $taskStateArgument = '"' + $taskState + '"'
    Invoke-ApplicationExitCheck 'initial-workspace' @('--fresh', '--smoke', '--state-dir', $taskStateArgument)
    Invoke-ApplicationExitCheck 'create-floating-window' @('--command', 'workspace.detachFloating', '--smoke', '--state-dir', $taskStateArgument)
    Invoke-ApplicationExitCheck 'create-second-workbench' @('--command', 'window.new', '--smoke', '--state-dir', $taskStateArgument)
    $taskBefore = Read-ExitSnapshot
    if ($taskBefore.model.windows.Count -ne 3) { throw 'Fixture must contain three windows.' }
    if (@($taskBefore.model.windows | Where-Object kind -eq 'floating').Count -ne 1) { throw 'Floating window was not created.' }
    $taskExpectedModel = $taskBefore.model | ConvertTo-Json -Depth 100 -Compress
    Invoke-ApplicationExitCheck 'exit-all-window-families' @('--command', 'application.exit', '--state-dir', $taskStateArgument)
    $taskAfterExit = Read-ExitSnapshot
    if (($taskAfterExit.model | ConvertTo-Json -Depth 100 -Compress) -ne $taskExpectedModel) { throw 'Exit lost part of the workspace.' }
    Invoke-ApplicationExitCheck 'restart-complete-workspace' @('--smoke', '--state-dir', $taskStateArgument)
    if (((Read-ExitSnapshot).model | ConvertTo-Json -Depth 100 -Compress) -ne $taskExpectedModel) { throw 'Restart changed the saved workspace.' }
    $taskBlockedState = Join-Path $taskFixture 'state-is-a-file'
    [IO.File]::WriteAllText($taskBlockedState, 'fixture')
    Invoke-ApplicationExitCheck 'report-save-failure' @('--fresh', '--command', 'application.exit', '--state-dir', ('"' + $taskBlockedState + '"')) 4
    if ((Get-Content -LiteralPath $taskBlockedState -Raw) -ne 'fixture') { throw 'Save failure changed the blocking file.' }
    @{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); sdkRemovedFromPath = $true;
       windowCount = 3; completeModelPreserved = $true; temporaryStateCleanedOnExit = $true;
       runs = $taskRuns.ToArray() } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $taskOutput 'test-results/application-exit.json') -Encoding utf8NoBOM
    Write-Host 'Application exit checks passed: three windows, restart and save failure.'
} finally {
    $env:PATH = $taskOriginalPath
    $env:QT_PLUGIN_PATH = $taskOriginalPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $taskOriginalPlatformPath
    Remove-OutputItem $taskFixture $taskOutput
}
