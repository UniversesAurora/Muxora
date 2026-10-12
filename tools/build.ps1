[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$QtRoot,
    [switch]$Test,
    [switch]$Run,
    [switch]$Package,
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'deployment.ps1')
$taskLocalFile = Join-Path $taskRepository '.local/sdk.json'
if (-not $QtRoot) { $QtRoot = $env:QT_ROOT }
if (-not $QtRoot -and (Test-Path -LiteralPath $taskLocalFile)) {
    $QtRoot = (Get-Content -LiteralPath $taskLocalFile -Raw | ConvertFrom-Json).qtRoot
}
if (-not $QtRoot) {
    $taskSiblingSdk = Join-Path (Split-Path -Parent $taskRepository) 'sdk/Qt'
    if (Test-Path -LiteralPath $taskSiblingSdk) {
        $QtRoot = Get-ChildItem -LiteralPath $taskSiblingSdk -Directory |
            Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'mingw_64' } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'lib/cmake/Qt6/Qt6Config.cmake') } |
            Select-Object -First 1
    }
}
if (-not $QtRoot -or -not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'Pass -QtRoot with the installed Qt kit directory (for example .../6.12.0/mingw_64).'
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$taskQtInstallation = Split-Path -Parent (Split-Path -Parent $QtRoot)
$taskTools = Join-Path $taskQtInstallation 'Tools'
$taskQtConfigFile = Join-Path $QtRoot 'mkspecs/qconfig.pri'
if (-not (Test-Path -LiteralPath $taskQtConfigFile)) { throw "Qt compiler metadata is missing: $taskQtConfigFile" }
$taskQtConfig = Get-Content -LiteralPath $taskQtConfigFile -Raw
$taskCompilerVersionParts = foreach ($taskVersionPart in @('MAJOR', 'MINOR', 'PATCH')) {
    $taskVersionMatch = [regex]::Match($taskQtConfig, "(?m)^QT_GCC_${taskVersionPart}_VERSION\s*=\s*(\d+)\s*$")
    if (-not $taskVersionMatch.Success) { throw "The selected Qt kit does not declare a MinGW GCC $taskVersionPart version." }
    $taskVersionMatch.Groups[1].Value
}
$taskRequiredCompilerVersion = $taskCompilerVersionParts -join '.'
$taskCompilerCandidates = @(Get-ChildItem -LiteralPath $taskTools -Directory -Filter 'mingw*' |
    ForEach-Object { Join-Path $_.FullName 'bin/g++.exe' } |
    Where-Object { Test-Path -LiteralPath $_ })
$taskCompiler = $null
$taskDetectedCompilers = foreach ($taskCompilerCandidate in $taskCompilerCandidates) {
    $taskDetectedVersion = (& $taskCompilerCandidate -dumpfullversion -dumpversion 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $taskDetectedVersion -notmatch '^\d+\.\d+\.\d+$') {
        throw "Unable to read GCC version from $taskCompilerCandidate."
    }
    if ($taskDetectedVersion -eq $taskRequiredCompilerVersion -and -not $taskCompiler) { $taskCompiler = $taskCompilerCandidate }
    "$taskDetectedVersion ($taskCompilerCandidate)"
}
if (-not $taskCompiler) {
    $taskInstalledCompilerDescription = if ($taskDetectedCompilers.Count) { $taskDetectedCompilers -join '; ' } else { 'none' }
    throw "This Qt kit was built with GCC $taskRequiredCompilerVersion. Install that matching Qt MinGW compiler before building. Installed compilers: $taskInstalledCompilerDescription"
}
$taskCompilerBin = Split-Path -Parent $taskCompiler
$taskCmake = Join-Path $taskTools 'CMake_64/bin/cmake.exe'
$taskNinja = Join-Path $taskTools 'Ninja/ninja.exe'
$taskCtest = Join-Path (Split-Path -Parent $taskCmake) 'ctest.exe'
foreach ($taskTool in @($taskCmake, $taskNinja, $taskCtest)) {
    if (-not (Test-Path -LiteralPath $taskTool)) { throw "Required build tool missing: $taskTool" }
}
$taskOriginalPath = $env:PATH
$env:PATH = "$QtRoot\bin;$taskCompilerBin;$(Split-Path -Parent $taskNinja);$taskOriginalPath"
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
try {
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskLocalFile) -Force | Out-Null
    @{ qtRoot = $QtRoot; compiler = $taskCompiler } | ConvertTo-Json |
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
    Push-Location $taskRepository
    try {
        Invoke-Checked $taskCmake @('--preset', $taskPreset)
        Invoke-Checked $taskCmake @('--build', $taskBuild, '--parallel', '4')
        if ($Test) {
            $taskTestOutput = Join-Path $taskRepository 'out/test-results'
            New-Item -ItemType Directory -Path $taskTestOutput -Force | Out-Null
            Invoke-Checked $taskCtest @('--test-dir', $taskBuild, '--output-on-failure',
                '--no-tests=error', '--output-junit', (Join-Path $taskTestOutput "$taskPreset.xml"))
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
                '--dir', $taskPackageDirectory, '--no-translations', '--compiler-runtime',
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
            $taskRuntimeLicenses = Join-Path (Split-Path -Parent $taskCompilerBin) 'licenses'
            if (Test-Path -LiteralPath $taskRuntimeLicenses) {
                Copy-Item -LiteralPath $taskRuntimeLicenses -Destination (Join-Path $taskPackageDirectory 'licenses/mingw') -Recurse -Force
            }
            @{ qt = $taskQtVersion; configuration = $Configuration;
               application = '0.1.0-window-preview'; compiler = "MinGW GCC $taskRequiredCompilerVersion";
               executableSha256 = (Get-FileHash -LiteralPath $taskPackagedExe -Algorithm SHA256).Hash } |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskPackageDirectory 'build-info.json') -Encoding utf8NoBOM
            $taskExecutable = $taskPackagedExe
            Write-DeploymentInventory $taskPackageDirectory
        }
        if ($Run) { Start-Process -FilePath $taskExecutable -WorkingDirectory (Split-Path -Parent $taskExecutable) }
        Write-Host "Ready: $taskExecutable"
    } finally { Pop-Location }
} finally { $env:PATH = $taskOriginalPath }
