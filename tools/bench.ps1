[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Name,
    [string]$Tabs = '100,500,1000',
    [ValidateRange(1, 100000)][int]$Repeat = 30,
    [switch]$Visible
)
# Builds the opt-in WindowSystem scale benchmark in its own Release directory
# and writes out/test-results/bench-<Name>.json. The default dev builds and the
# fixed deployment directory are never touched.
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
$taskLocalFile = Join-Path $taskRepository '.local/sdk.json'
if (-not (Test-Path -LiteralPath $taskLocalFile)) {
    throw "Missing $taskLocalFile. Run tools/build.ps1 once so it records the Qt kit and compiler."
}
$taskSdk = Get-Content -LiteralPath $taskLocalFile -Raw | ConvertFrom-Json
if (-not $taskSdk.qtRoot -or -not $taskSdk.compiler) {
    throw "$taskLocalFile lacks qtRoot or compiler. Run tools/build.ps1 again."
}
$taskQt = $taskSdk.qtRoot
$taskTools = Join-Path (Split-Path -Parent (Split-Path -Parent $taskQt)) 'Tools'
$taskCmake = Join-Path $taskTools 'CMake_64/bin/cmake.exe'
$taskNinja = Join-Path $taskTools 'Ninja/ninja.exe'
foreach ($taskTool in @($taskCmake, $taskNinja, $taskSdk.compiler)) {
    if (-not (Test-Path -LiteralPath $taskTool)) { throw "Required build tool missing: $taskTool" }
}
$taskBuild = Join-Path $taskRepository 'out/build/bench-release'
$taskResults = Join-Path $taskRepository 'out/test-results'
$taskOutput = Join-Path $taskResults "bench-$Name.json"
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$taskOriginalPath = $env:PATH
$taskOriginalPlatform = $env:QT_QPA_PLATFORM
$env:PATH = "$taskQt\bin;$(Split-Path -Parent $taskSdk.compiler);$(Split-Path -Parent $taskNinja);$taskOriginalPath"
try {
    Invoke-Checked $taskCmake @('-S', $taskRepository, '-B', $taskBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$taskQt",
        "-DCMAKE_CXX_COMPILER=$($taskSdk.compiler)", "-DCMAKE_MAKE_PROGRAM=$taskNinja",
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
    Invoke-Checked $taskBenchmark @('--tabs', $Tabs, '--repeat', "$Repeat",
        '--commit', $taskCommit, '--out', $taskOutput)
    Write-Host "Benchmark results: $taskOutput"
} finally {
    $env:PATH = $taskOriginalPath
    if ($null -eq $taskOriginalPlatform) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $taskOriginalPlatform }
}
