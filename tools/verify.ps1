[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'tests/deployment-test.ps1')
$taskBuildArguments = @{}
if ($QtRoot) { $taskBuildArguments.QtRoot = $QtRoot }
$taskPackage = if ($PackageDirectory) {
    [IO.Path]::GetFullPath($PackageDirectory, $taskRepository)
} else {
    Join-Path $taskRepository 'out/package/muxora-preview'
}
$taskReleaseArguments = $taskBuildArguments.Clone()
if ($PackageDirectory) { $taskReleaseArguments.PackageDirectory = $taskPackage }
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Debug -Test @taskBuildArguments
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Test -Package @taskReleaseArguments
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$taskSdk = Get-Content -LiteralPath (Join-Path $taskRepository '.local/sdk.json') -Raw | ConvertFrom-Json
$taskQt = $taskSdk.qtRoot
$taskToolchain = Enter-Toolchain $taskQt
$taskCmake = $taskToolchain.Cmake
$taskCtest = $taskToolchain.Ctest
$taskNinja = $taskToolchain.Ninja
function Invoke-Verification([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Verification failed ($LASTEXITCODE): $Program" }
}
try {
    $taskStandalone = Join-Path $taskRepository 'out/build/standalone'
    $taskInstall = Join-Path $taskRepository 'out/install/window-system'
    $taskExternal = Join-Path $taskRepository 'out/build/external-host'
    $taskCommon = @('-G', 'Ninja', "-DCMAKE_CXX_COMPILER=$($taskToolchain.Compiler)",
        "-DCMAKE_MAKE_PROGRAM=$taskNinja", '-DCMAKE_BUILD_TYPE=Release')
    Invoke-Verification $taskCmake (@('-S', (Join-Path $taskRepository 'modules/window-system'),
        '-B', $taskStandalone, "-DCMAKE_PREFIX_PATH=$taskQt", '-DWINDOW_SYSTEM_BUILD_TESTS=ON') + $taskCommon)
    Invoke-Verification $taskCmake @('--build', $taskStandalone, '--parallel', '4')
    Invoke-Verification $taskCtest @('--test-dir', $taskStandalone, '--output-on-failure', '--no-tests=error',
        '--output-junit', (Join-Path $taskRepository 'out/test-results/standalone.xml'))
    Invoke-Verification $taskCmake @('--install', $taskStandalone, '--prefix', $taskInstall)
    Invoke-Verification $taskCmake (@('-S', (Join-Path $taskRepository 'modules/window-system/tests/fixtures/external-host'),
        '-B', $taskExternal, "-DCMAKE_PREFIX_PATH=$taskInstall;$taskQt") + $taskCommon)
    Invoke-Verification $taskCmake @('--build', $taskExternal, '--parallel', '4')
    Invoke-Verification $taskCtest @('--test-dir', $taskExternal, '--output-on-failure', '--no-tests=error',
        '--output-junit', (Join-Path $taskRepository 'out/test-results/external-host.xml'))
    # The packaged application must start without SDK or compiler directories on
    # PATH; its deployment directory is the only source of Qt/runtime DLLs.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $taskSmokeState = Join-Path $taskRepository 'out/package smoke state'
    $taskLaunch = @{
        FilePath = Join-Path $taskPackage 'muxora.exe'
        ArgumentList = @('--fresh', '--smoke', '--state-dir', ('"' + $taskSmokeState + '"'))
        WorkingDirectory = $taskPackage
        Wait = $true
        PassThru = $true
        WindowStyle = 'Hidden'
    }
    $taskProcess = Start-Process @taskLaunch
    if ($taskProcess.ExitCode -ne 0) { throw "Packaged application smoke failed: $($taskProcess.ExitCode)" }
    $taskSmokeLayout = Join-Path $taskSmokeState 'window-layout.json'
    if (-not (Test-Path -LiteralPath $taskSmokeLayout)) { throw 'Packaged application did not save its smoke layout.' }
    $taskSmokeSnapshot = Get-Content -LiteralPath $taskSmokeLayout -Raw | ConvertFrom-Json
    if (-not $taskSmokeSnapshot.model) { throw 'Packaged application saved an invalid smoke layout.' }
    @{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); exitCode = $taskProcess.ExitCode;
       sdkRemovedFromPath = $true; statePathWithSpaces = $true } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRepository 'out/test-results/package-smoke.json') -Encoding utf8NoBOM
    & (Join-Path $PSScriptRoot 'tests/application-exit-test.ps1') -PackageDirectory $taskPackage
    & (Join-Path $PSScriptRoot 'tests/application-view-test.ps1') -PackageDirectory $taskPackage
    Write-Host 'Verification passed: debug, release, standalone, installed API, deployed application, exit/restart and view-state restart.'
} finally { Exit-Toolchain $taskToolchain }
