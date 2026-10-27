[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$QtRoot,
    [switch]$Test,
    # CTest label regex for -Test: model, widget, desktop or a combination such as 'model|widget'.
    [string]$TestLabel,
    [switch]$Run,
    [switch]$Package,
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'deployment.ps1')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$taskLocalFile = Join-Path $taskRepository '.local/sdk.json'
$QtRoot = Resolve-QtKit $QtRoot $taskRepository
$taskQtInstallation = Split-Path -Parent (Split-Path -Parent $QtRoot)
$taskToolchain = Enter-Toolchain $QtRoot
$taskCompiler = $taskToolchain.Compiler
$taskCmake = $taskToolchain.Cmake
$taskNinja = $taskToolchain.Ninja
$taskCtest = $taskToolchain.Ctest
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
try {
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskLocalFile) -Force | Out-Null
    @{ qtRoot = $QtRoot; compiler = $taskCompiler; toolchain = $taskToolchain.Kind } | ConvertTo-Json |
        Set-Content -LiteralPath $taskLocalFile -Encoding utf8NoBOM
    $taskUserPresets = @{
        version = 6
        configurePresets = @(
            @{ name = 'dev-debug'; inherits = 'debug'; cacheVariables = @{
                CMAKE_PREFIX_PATH = $QtRoot; CMAKE_CXX_COMPILER = $taskCompiler; CMAKE_MAKE_PROGRAM = $taskNinja } },
            @{ name = 'dev-release'; inherits = 'release'; cacheVariables = @{
                CMAKE_PREFIX_PATH = $QtRoot; CMAKE_CXX_COMPILER = $taskCompiler; CMAKE_MAKE_PROGRAM = $taskNinja } }
        )
    }
    $taskUserPresets | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $taskRepository 'CMakeUserPresets.json') -Encoding utf8NoBOM
    $taskPreset = 'dev-' + $Configuration.ToLowerInvariant()
    $taskBuild = Join-Path $taskRepository "out/build/$taskPreset"
    if ($taskStale = Get-StaleBuildReason $taskToolchain $taskBuild) {
        Write-Host "Build directory is stale: $taskStale; clearing $taskBuild"
        Remove-OutputItem $taskBuild (Join-Path $taskRepository 'out')
    }
    Push-Location $taskRepository
    try {
        Invoke-Checked $taskCmake @('--preset', $taskPreset)
        Invoke-Checked $taskCmake @('--build', $taskBuild, '--parallel', '4')
        if ($Test) {
            $taskTestOutput = Join-Path $taskRepository 'out/test-results'
            New-Item -ItemType Directory -Path $taskTestOutput -Force | Out-Null
            $taskCtestArguments = @('--test-dir', $taskBuild, '--output-on-failure', '--no-tests=error')
            $taskJunitName = $taskPreset
            if ($TestLabel) {
                $taskCtestArguments += @('-L', $TestLabel)
                $taskJunitName += '-' + ($TestLabel -replace '[^A-Za-z0-9_-]+', '-')
            }
            $taskCtestArguments += @('--output-junit', (Join-Path $taskTestOutput "$taskJunitName.xml"))
            Invoke-Checked $taskCtest $taskCtestArguments
        }
        $taskExecutable = Join-Path $taskBuild 'bin/muxora.exe'
        if ($Package) {
            $taskPackageDirectory = if ($PackageDirectory) {
                [IO.Path]::GetFullPath($PackageDirectory, $taskRepository)
            } else {
                Join-Path $taskRepository 'out/package/muxora-preview'
            }
            Reset-ManagedDeployment $taskPackageDirectory (Join-Path $taskRepository 'out')
            Copy-Item -LiteralPath $taskExecutable -Destination $taskPackageDirectory -Force
            $taskPackagedExe = Join-Path $taskPackageDirectory 'muxora.exe'
            Invoke-Checked (Join-Path $QtRoot 'bin/windeployqt.exe') @(
                '--dir', $taskPackageDirectory, '--no-translations',
                # The Visual C++ runtime is copied below from Visual Studio's redist directory.
                '--no-compiler-runtime',
                # DirectX Shader Compiler serves Qt's D3D12 rendering, which the
                # preview does not use yet (GPU terminal rendering arrives in 7.2).
                '--no-system-dxc-compiler',
                '--skip-plugin-types', 'imageformats,iconengines,networkinformation,tls',
                ('--' + $Configuration.ToLowerInvariant()), $taskPackagedExe)
            Copy-Item -LiteralPath (Join-Path $taskRepository 'LICENSE') -Destination $taskPackageDirectory -Force
            if (Test-Path -LiteralPath (Join-Path $taskRepository 'THIRD_PARTY_NOTICES.md')) {
                Copy-Item -LiteralPath (Join-Path $taskRepository 'THIRD_PARTY_NOTICES.md') -Destination $taskPackageDirectory -Force
            }
            $taskLicenses = Join-Path $taskQtInstallation 'Licenses'
            if (Test-Path -LiteralPath $taskLicenses) {
                Copy-Item -LiteralPath $taskLicenses -Destination (Join-Path $taskPackageDirectory 'licenses') -Recurse -Force
            }
            Copy-Item -LiteralPath (Join-Path $taskRepository 'third_party/licenses') -Destination (Join-Path $taskPackageDirectory 'licenses/open-source') -Recurse -Force
            $taskQtVersion = (& (Join-Path $QtRoot 'bin/qmake.exe') -query QT_VERSION | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $taskQtVersion -notmatch '^\d+\.\d+\.\d+$') {
                throw 'Unable to read the Qt version for the deployment inventory.'
            }
            $taskSbom = Join-Path $QtRoot "sbom/qtbase-$taskQtVersion.spdx.json"
            if (Test-Path -LiteralPath $taskSbom) {
                Copy-Item -LiteralPath $taskSbom -Destination (Join-Path $taskPackageDirectory 'licenses/qtbase-sbom.spdx.json') -Force
            }
            # App-local Visual C++ runtime: the preview runs without installing vc_redist.
            Copy-MsvcRuntime $taskToolchain $taskPackageDirectory
            $taskBuildInfo = @{ qt = $taskQtVersion; configuration = $Configuration;
                application = '0.1.0-window-preview'; compiler = $taskToolchain.Description }
            $taskBuildInfo |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskPackageDirectory 'build-info.json') -Encoding utf8NoBOM
            $taskExecutable = $taskPackagedExe
            Write-DeploymentInventory $taskPackageDirectory
        }
        if ($Run) { Start-Process -FilePath $taskExecutable -WorkingDirectory (Split-Path -Parent $taskExecutable) }
        Write-Host "Ready: $taskExecutable"
    } finally { Pop-Location }
} finally { Exit-Toolchain $taskToolchain }
