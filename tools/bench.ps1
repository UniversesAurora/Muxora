[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Name,
    [string]$Tabs = '100,500,1000',
    [ValidateRange(1, 100000)][int]$Repeat = 30,
    [ValidateSet('all', 'restore', 'create', 'close', 'move', 'split', 'ratio', 'keys', 'save', 'cycle')]
    [string[]]$Scenario = @(),
    [switch]$Visible,
    [string]$QtRoot
)
# Builds the opt-in WindowSystem scale benchmark in its own Release directory
# and writes out/test-results/bench-<Name>.json. The default dev builds and the
# fixed deployment directory are never touched.
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'deployment.ps1')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$taskQt = Resolve-QtKit $QtRoot $taskRepository
$taskBuild = Join-Path $taskRepository 'out/build/bench-release'
$taskResults = Join-Path $taskRepository 'out/test-results'
$taskOutput = Join-Path $taskResults "bench-$Name.json"
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$taskOriginalPlatform = $env:QT_QPA_PLATFORM
$taskToolchain = Enter-Toolchain $taskQt
$taskCmake = $taskToolchain.Cmake
$taskNinja = $taskToolchain.Ninja
try {
    if ($taskStale = Get-StaleBuildReason $taskToolchain $taskBuild) {
        Write-Host "Build directory is stale: $taskStale; clearing $taskBuild"
        Remove-OutputItem $taskBuild (Join-Path $taskRepository 'out')
    }
    Invoke-Checked $taskCmake @('-S', $taskRepository, '-B', $taskBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$taskQt",
        "-DCMAKE_CXX_COMPILER=$($taskToolchain.Compiler)", "-DCMAKE_MAKE_PROGRAM=$taskNinja",
        '-DBUILD_TESTING=OFF', '-DWINDOW_SYSTEM_BUILD_BENCHMARKS=ON')
    Invoke-Checked $taskCmake @('--build', $taskBuild, '--parallel', '4', '--target', 'window-system-layout-benchmark')
    $taskBenchmark = Join-Path $taskBuild 'modules/window-system/benchmarks/window-system-layout-benchmark.exe'
    if (-not (Test-Path -LiteralPath $taskBenchmark)) { throw "Benchmark executable missing: $taskBenchmark" }
    $taskCommit = (& git -C $taskRepository rev-parse --short HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $taskCommit) { throw 'Unable to read the current commit.' }
    if (& git -C $taskRepository status --porcelain --untracked-files=no) { $taskCommit += '-dirty' }
    New-Item -ItemType Directory -Path $taskResults -Force | Out-Null
    if ($Visible) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = 'offscreen' }
    $taskBenchmarkArguments = @('--tabs', $Tabs, '--repeat', "$Repeat")
    foreach ($taskScenario in $Scenario) {
        $taskBenchmarkArguments += @('--scenario', $taskScenario)
    }
    $taskBenchmarkArguments += @('--commit', $taskCommit, '--out', $taskOutput)
    Invoke-Checked $taskBenchmark $taskBenchmarkArguments
    Write-Host "Benchmark results: $taskOutput"
} finally {
    Exit-Toolchain $taskToolchain
    if ($null -eq $taskOriginalPlatform) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $taskOriginalPlatform }
}
