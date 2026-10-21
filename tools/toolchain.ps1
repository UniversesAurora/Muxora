# Shared Qt kit and compiler discovery for the build, verification and
# benchmark scripts. Dot-source this file; it only defines functions.
#
# The project builds with MSVC only (ADR 0009): the Qt kit must be an MSVC kit,
# used with the installed Visual Studio toolset loaded through vcvars64.

function Resolve-QtKit([string]$QtRoot, [string]$Repository) {
    if (-not $QtRoot) { $QtRoot = $env:QT_ROOT }
    if (-not $QtRoot) {
        # Newest Qt version beside the repository that has an MSVC kit.
        $taskSiblingSdk = Join-Path (Split-Path -Parent $Repository) 'sdk/Qt'
        if (Test-Path -LiteralPath $taskSiblingSdk) {
            $QtRoot = Get-ChildItem -LiteralPath $taskSiblingSdk -Directory |
                Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending |
                ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ 'lib/cmake/Qt6/Qt6Config.cmake') } |
                Select-Object -First 1
        }
    }
    if (-not $QtRoot) {
        $taskLocalFile = Join-Path $Repository '.local/sdk.json'
        if (Test-Path -LiteralPath $taskLocalFile) { $QtRoot = (Get-Content -LiteralPath $taskLocalFile -Raw | ConvertFrom-Json).qtRoot }
    }
    if (-not $QtRoot -or -not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
        throw 'Pass -QtRoot with the installed Qt kit directory (for example .../6.12.0/msvc2022_64).'
    }
    return (Resolve-Path -LiteralPath $QtRoot).Path
}

function Read-QtCompilerVersion([string]$QtRoot, [string]$Prefix) {
    $taskConfig = Get-Content -LiteralPath (Join-Path $QtRoot 'mkspecs/qconfig.pri') -Raw
    $taskParts = foreach ($taskPart in @('MAJOR', 'MINOR', 'PATCH')) {
        $taskMatch = [regex]::Match($taskConfig, "(?m)^QT_${Prefix}_${taskPart}_VERSION\s*=\s*(\d+)\s*$")
        if (-not $taskMatch.Success) { return $null }
        $taskMatch.Groups[1].Value
    }
    return [version]($taskParts -join '.')
}

# Loads vcvars64 of the newest Visual Studio with the x64 C++ tools into this
# process. Returns the variables it changed so Exit-Toolchain can restore them.
function Import-VisualStudioEnvironment {
    $taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $taskVsWhere)) { throw 'Visual Studio (vswhere.exe) was not found. Install Visual Studio with the C++ desktop workload.' }
    $taskInstallation = (& $taskVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Out-String).Trim()
    if (-not $taskInstallation) { throw 'No Visual Studio installation with the x64 C++ tools was found.' }
    $taskVcVars = Join-Path $taskInstallation 'VC/Auxiliary/Build/vcvars64.bat'
    if (-not (Test-Path -LiteralPath $taskVcVars)) { throw "vcvars64.bat is missing: $taskVcVars" }
    $taskLines = & cmd.exe /d /c "`"$taskVcVars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed: $taskVcVars" }
    $taskSaved = @{}
    foreach ($taskLine in $taskLines) {
        $taskSeparator = $taskLine.IndexOf('=')
        if ($taskSeparator -le 0) { continue }
        $taskName = $taskLine.Substring(0, $taskSeparator)
        $taskValue = $taskLine.Substring($taskSeparator + 1)
        $taskCurrent = [Environment]::GetEnvironmentVariable($taskName, 'Process')
        if ($taskCurrent -ne $taskValue) {
            $taskSaved[$taskName] = $taskCurrent
            [Environment]::SetEnvironmentVariable($taskName, $taskValue, 'Process')
        }
    }
    return $taskSaved
}

function Get-MsvcCompilerVersion([string]$Compiler) {
    # cl prints its banner, including the version, to standard error and its
    # usage line to standard output, in no fixed order: search all of it.
    $taskBanner = (& $Compiler 2>&1 | Out-String)
    $taskMatch = [regex]::Match($taskBanner, '(\d+)\.(\d+)\.(\d+)')
    if (-not $taskMatch.Success) { throw "Unable to read the MSVC version from: $taskBanner" }
    return [version]$taskMatch.Value
}

# Prepares the compiler for the given Qt kit in this process. The returned
# object carries the compiler, the build tools and the state to restore.
function Enter-Toolchain([string]$QtRoot) {
    $taskQtInstallation = Split-Path -Parent (Split-Path -Parent $QtRoot)
    $taskTools = Join-Path $taskQtInstallation 'Tools'
    $taskCmake = Join-Path $taskTools 'CMake_64/bin/cmake.exe'
    $taskNinja = Join-Path $taskTools 'Ninja/ninja.exe'
    $taskCtest = Join-Path (Split-Path -Parent $taskCmake) 'ctest.exe'
    foreach ($taskTool in @($taskCmake, $taskNinja, $taskCtest)) {
        if (-not (Test-Path -LiteralPath $taskTool)) { throw "Required build tool missing: $taskTool" }
    }
    $taskState = [ordered]@{
        QtRoot = $QtRoot; QtInstallation = $taskQtInstallation; Cmake = $taskCmake; Ninja = $taskNinja; Ctest = $taskCtest
        Kind = $null; Compiler = $null; CompilerVersion = $null; Description = $null
        RuntimeDirectory = $null
        SavedEnvironment = @{ PATH = $env:PATH }
    }
    $taskMsvcVersion = Read-QtCompilerVersion $QtRoot 'MSVC'
    if (-not $taskMsvcVersion) {
        throw "Not an MSVC Qt kit (no QT_MSVC_* in mkspecs/qconfig.pri): $QtRoot. Install Qt's MSVC 2022 64-bit kit."
    }
    $taskSaved = Import-VisualStudioEnvironment
    foreach ($taskName in $taskSaved.Keys) {
        if (-not $taskState.SavedEnvironment.Contains($taskName)) { $taskState.SavedEnvironment[$taskName] = $taskSaved[$taskName] }
    }
    $taskCompiler = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
    if (-not $taskCompiler) { throw 'vcvars64 did not put cl.exe on PATH.' }
    $taskVersion = Get-MsvcCompilerVersion $taskCompiler
    # Microsoft's binary compatibility rule: the toolset that links must be
    # at least as new as the newest toolset that built any linked binary.
    if ($taskVersion -lt $taskMsvcVersion) {
        throw "This Qt kit was built with MSVC $taskMsvcVersion; the installed compiler is $taskVersion. Update Visual Studio."
    }
    $taskState.Kind = 'msvc'
    $taskState.Compiler = $taskCompiler
    $taskState.CompilerVersion = $taskVersion
    $taskState.Description = "MSVC $taskVersion"
    $taskState.RuntimeDirectory = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
    if (-not (Test-Path -LiteralPath $taskState.RuntimeDirectory)) {
        throw "Visual C++ runtime redistributable directory is missing: $($taskState.RuntimeDirectory)"
    }
    $env:PATH = "$QtRoot\bin;$(Split-Path -Parent $taskNinja);$env:PATH"
    return [pscustomobject]$taskState
}

function Exit-Toolchain($Toolchain) {
    if (-not $Toolchain) { return }
    foreach ($taskName in $Toolchain.SavedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskName, $Toolchain.SavedEnvironment[$taskName], 'Process')
    }
}

# Copies the Visual C++ runtime DLLs beside the application (app-local
# deployment, permitted by the Visual Studio license for these files).
function Copy-MsvcRuntime($Toolchain, [string]$Destination) {
    foreach ($taskFile in Get-ChildItem -LiteralPath $Toolchain.RuntimeDirectory -Filter '*.dll') {
        Copy-Item -LiteralPath $taskFile.FullName -Destination $Destination -Force
    }
}
