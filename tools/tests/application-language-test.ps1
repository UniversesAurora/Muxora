[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepository 'tools/deployment.ps1')
$taskOutput = Join-Path $taskRepository 'out'
$taskFixture = Join-Path $taskOutput 'application-language-checks/current'
$taskReports = Join-Path $taskOutput 'test-results'
$taskExecutable = Join-Path ([IO.Path]::GetFullPath($PackageDirectory, $taskRepository)) 'muxora.exe'
if (-not (Test-Path -LiteralPath $taskExecutable -PathType Leaf)) { throw 'Deployed executable is missing.' }
$taskOriginalEnvironment = @{}
foreach ($taskName in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    $taskOriginalEnvironment[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
}
$taskProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$taskResults = [Collections.Generic.List[object]]::new()
$taskPassed = $false
try {
    Remove-OutputItem $taskFixture $taskOutput
    Remove-OutputItem (Join-Path $taskReports 'application-language.json') $taskOutput
    New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    foreach ($taskLanguage in @('en', 'zh_CN')) {
        $taskState = Join-Path $taskFixture ("language $taskLanguage")
        $taskUser = Join-Path $taskState 'User'
        New-Item -ItemType Directory -Path $taskUser -Force | Out-Null
        @{ 'workbench.language' = $taskLanguage } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $taskUser 'settings.json') -Encoding utf8NoBOM
        $taskStdout = Join-Path $taskReports "application-language-$taskLanguage.log"
        $taskStderr = Join-Path $taskReports "application-language-$taskLanguage-error.log"
        $taskProcess = Start-Process -FilePath $taskExecutable -WindowStyle Hidden -PassThru `
            -WorkingDirectory (Split-Path -Parent $taskExecutable) `
            -ArgumentList @('--state-dir', ('"' + $taskState + '"'), '--help') `
            -RedirectStandardOutput $taskStdout -RedirectStandardError $taskStderr
        $taskProcesses.Add($taskProcess)
        if (-not $taskProcess.WaitForExit(15000)) { throw "Deployed help did not exit: $taskLanguage" }
        $taskProcess.Refresh()
        if ($taskProcess.ExitCode -ne 0) { throw "Deployed help failed: $taskLanguage ($($taskProcess.ExitCode))" }
        $taskHelp = Get-Content -LiteralPath $taskStdout -Raw -Encoding utf8
        $taskDescription = if ($taskLanguage -eq 'en') { 'Muxora window system preview' } else { 'Muxora 窗口系统预览' }
        $taskFresh = if ($taskLanguage -eq 'en') { 'Use the initial layout without loading saved state' } else { '使用初始布局，不读取保存状态' }
        foreach ($taskText in @($taskDescription, $taskFresh, '--fresh', '--state-dir', '--command')) {
            if (-not $taskHelp.Contains($taskText)) { throw "Deployed help is missing '$taskText': $taskLanguage" }
        }
        if (Test-Path -LiteralPath (Join-Path $taskState 'window-layout.json')) { throw 'Help unexpectedly saved a workspace.' }
        $taskResults.Add(@{ language = $taskLanguage; exitCode = $taskProcess.ExitCode;
            translatedHelp = $true; stableOptions = $true; noWorkspaceCreated = $true })
    }
    $taskPassed = $true
} finally {
    foreach ($taskName in $taskOriginalEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskName, $taskOriginalEnvironment[$taskName], 'Process')
    }
    $taskAllStopped = $true
    foreach ($taskProcess in $taskProcesses) {
        if (-not $taskProcess.HasExited) {
            $null = $taskProcess.CloseMainWindow()
            if (-not $taskProcess.WaitForExit(5000)) { $taskAllStopped = $false }
        }
        $taskProcess.Dispose()
    }
    if ($taskPassed -and $taskAllStopped) { Remove-OutputItem $taskFixture $taskOutput }
    if (-not $taskAllStopped) { throw 'A language-check process did not close; its fixture was preserved.' }
}
@{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); sdkRemovedFromPath = $true; runs = $taskResults.ToArray() } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskReports 'application-language.json') -Encoding utf8NoBOM
Write-Host 'Application language checks passed: English and Simplified Chinese help without SDK paths.'
