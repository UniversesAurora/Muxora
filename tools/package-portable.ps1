[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$SevenZip
)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'deployment.ps1')
$taskOutputRoot = Join-Path $taskRepository 'out'
if (-not $SevenZip) {
    $taskCommand = Get-Command 7z, 7zz -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($taskCommand) { $SevenZip = $taskCommand.Source }
}
if (-not $SevenZip -or -not (Test-Path -LiteralPath $SevenZip -PathType Leaf)) {
    throw 'Install 7-Zip or pass -SevenZip with its executable path.'
}
$taskSevenZip = (Resolve-Path -LiteralPath $SevenZip).Path
$taskStage = [IO.Path]::GetFullPath((Join-Path $taskRepository 'out/package/portable-input'))
$taskPackage = Join-Path $taskStage 'Muxora'
$taskSourcePackage = [IO.Path]::GetFullPath((Join-Path $taskRepository 'out/package/muxora-preview'))
$taskCheckRoot = Join-Path $taskRepository 'out/portable-checks/current'
$taskStageOwned = $false
$taskCheckOwned = $false
$taskBuildArguments = @{ Configuration = 'Release'; Test = $true; Package = $true }
if ($QtRoot) { $taskBuildArguments.QtRoot = $QtRoot }
& (Join-Path $PSScriptRoot 'build.ps1') @taskBuildArguments

# Copy only the generated inventory, never a user's data/ or unknown files in
# the reusable deployment. Scratch paths and the archive are fixed on each run.
Assert-DeploymentStopped $taskPackage
if (Test-Path -LiteralPath $taskStage) {
    $taskUnknownRoot = @(Get-ChildItem -LiteralPath $taskStage -Force | Where-Object Name -ne 'Muxora')
    if ($taskUnknownRoot.Count) { throw 'Portable scratch directory contains unknown files.' }
    if (Test-Path -LiteralPath $taskPackage) {
        $taskManaged = @((Get-ManagedDeploymentItems $taskPackage).Name) + @('使用说明.txt', 'data')
        $taskUnknown = @(Get-ChildItem -LiteralPath $taskPackage -Force | Where-Object Name -notin $taskManaged)
        if ($taskUnknown.Count -or @(Get-ChildItem -LiteralPath (Join-Path $taskPackage 'data') -Force -ErrorAction SilentlyContinue).Count) {
            throw 'Portable input contains user data or unknown files; it was left intact.'
        }
    }
    Remove-OutputItem $taskStage $taskOutputRoot
}
try {
New-Item -ItemType Directory -Path $taskPackage -Force | Out-Null
$taskStageOwned = $true
$taskInventory = Get-Content -LiteralPath (Join-Path $taskSourcePackage 'deployment-files.json') -Raw | ConvertFrom-Json
foreach ($taskRelative in $taskInventory.files) {
    $taskSource = Assert-OutputPath ([IO.Path]::GetFullPath($taskRelative, $taskSourcePackage)) $taskOutputRoot
    $taskTarget = Assert-OutputPath ([IO.Path]::GetFullPath($taskRelative, $taskPackage)) $taskOutputRoot
    if (!$taskSource.StartsWith($taskSourcePackage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        !$taskTarget.StartsWith($taskPackage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Deployment inventory contains an invalid relative path.'
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskTarget) -Force | Out-Null
    Copy-Item -LiteralPath $taskSource -Destination $taskTarget
}
New-Item -ItemType Directory -Path (Join-Path $taskPackage 'data') | Out-Null
$taskBuildInfo = Get-Content -LiteralPath (Join-Path $taskPackage 'build-info.json') -Raw | ConvertFrom-Json
if ($taskBuildInfo.configuration -ne 'Release') { throw 'Only Release builds can be packaged here.' }
$taskArchiveName = 'Muxora-preview-windows-x64'
$taskDist = Join-Path $taskRepository 'out/dist'
New-Item -ItemType Directory -Path $taskDist -Force | Out-Null
$taskArchive = Join-Path $taskDist "$taskArchiveName.7z"
Remove-OutputItem $taskArchive $taskOutputRoot
Remove-OutputItem "$taskArchive.sha256" $taskOutputRoot
$taskReadme = @'
Muxora 窗口系统预览 — Windows x64 便携版

解压整个 Muxora 文件夹，然后双击 muxora.exe。无需安装 Qt 或编译器。
窗口布局和行为设置保存在相邻的 data/window-layout.json 中。
移动整个文件夹即可带走布局；请将文件夹放在可写位置。

Ctrl+Shift+P：命令面板
Ctrl+T：新建标签
Ctrl+,：窗口设置
Ctrl+Alt+方向键：标签组导航
Alt+方向键：窗格导航
Ctrl+Shift+E / Ctrl+Shift+O：向右 / 向下拆分窗格

标签拖出立即形成浮窗，Esc 恢复原布局。投放到停靠指示或标签条时合并。
首次启动使用初始布局；恢复初始布局可从命令面板操作。

本版供窗口交互试用，SSH、SFTP 和真实终端尚未接入。
编辑区文字和用户修改的快捷键暂不随退出保存。
源码与许可证说明见 LICENSE、THIRD_PARTY_NOTICES.md 和 licenses/。
'@
[IO.File]::WriteAllText((Join-Path $taskPackage '使用说明.txt'), $taskReadme, [Text.UTF8Encoding]::new($true))

function Invoke-Archive([string[]]$Arguments) {
    & $taskSevenZip @Arguments
    if ($LASTEXITCODE -ne 0) { throw "7-Zip failed with exit code $LASTEXITCODE." }
}
Push-Location $taskStage
try { Invoke-Archive @('a', '-t7z', '-mx=7', '-mmt=on', '-bsp0', '-bso0', $taskArchive, 'Muxora') }
finally { Pop-Location }
Invoke-Archive @('t', '-bso0', $taskArchive)

# Verify the archive itself after extraction, including a path with spaces and
# non-ASCII characters. The child process cannot inherit SDK/plugin overrides.
Remove-OutputItem $taskCheckRoot $taskOutputRoot
$taskCheck = Join-Path $taskCheckRoot '便携包 解压检查'
$taskCheckOwned = $true
Invoke-Archive @('x', '-y', '-bso0', "-o$taskCheck", $taskArchive)
$taskExtracted = Join-Path $taskCheck 'Muxora'
$taskExecutable = Join-Path $taskExtracted 'muxora.exe'
if ((Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash -ne $taskBuildInfo.executableSha256) {
    throw 'The extracted executable does not match the Release build.'
}
function Invoke-PortableSmoke([bool]$Fresh) {
    $taskStart = [Diagnostics.ProcessStartInfo]::new()
    $taskStart.FileName = $taskExecutable
    $taskStart.WorkingDirectory = $taskExtracted
    $taskStart.UseShellExecute = $false
    $taskStart.CreateNoWindow = $true
    $taskStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $taskStart.RedirectStandardOutput = $true
    $taskStart.RedirectStandardError = $true
    $taskStart.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($taskVariable in @('QT_ROOT', 'QTDIR', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
        'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM', 'QT_STYLE_OVERRIDE', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS')) {
        $taskStart.Environment.Remove($taskVariable) | Out-Null
    }
    $taskStart.ArgumentList.Add('--smoke')
    if ($Fresh) { $taskStart.ArgumentList.Add('--fresh') }
    $taskProcess = [Diagnostics.Process]::Start($taskStart)
    try {
        $taskOutput = $taskProcess.StandardOutput.ReadToEndAsync()
        $taskError = $taskProcess.StandardError.ReadToEndAsync()
        if (-not $taskProcess.WaitForExit(15000)) {
            $taskProcess.Kill()
            throw 'Extracted portable application did not complete its startup check within 15 seconds.'
        }
        $taskOutput.GetAwaiter().GetResult() | Out-Null
        $taskDiagnostics = $taskError.GetAwaiter().GetResult()
        if ($taskProcess.ExitCode -ne 0) { throw "Portable startup failed ($($taskProcess.ExitCode)): $taskDiagnostics" }
    } finally { $taskProcess.Dispose() }
}
Invoke-PortableSmoke $true
$taskLayoutFile = Join-Path $taskExtracted 'data/window-layout.json'
if (-not (Test-Path -LiteralPath $taskLayoutFile)) { throw 'Portable application did not save its layout under data/.' }
$taskInitial = Get-Content -LiteralPath $taskLayoutFile -Raw | ConvertFrom-Json
if (-not $taskInitial.model) { throw 'Portable application saved an invalid layout.' }
Invoke-PortableSmoke $false
$taskRestored = Get-Content -LiteralPath $taskLayoutFile -Raw | ConvertFrom-Json
if (($taskInitial.model | ConvertTo-Json -Depth 100 -Compress) -ne
    ($taskRestored.model | ConvertTo-Json -Depth 100 -Compress)) {
    throw 'Portable restart did not preserve the saved layout model.'
}
$taskHash = (Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash
[IO.File]::WriteAllText("$taskArchive.sha256", "$taskHash  $taskArchiveName.7z`n", [Text.UTF8Encoding]::new($false))
@{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); archive = "$taskArchiveName.7z";
   archiveSha256 = $taskHash; executableSha256 = $taskBuildInfo.executableSha256;
   archiveIntegrity = $true; extractedStartupExitCode = 0; restoredStartupExitCode = 0;
   sdkRemovedFromPath = $true; localStateSaved = $true; layoutRestored = $true;
   nonAsciiPathWithSpaces = $true } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $taskRepository 'out/test-results/portable-package.json') -Encoding utf8NoBOM
Write-Host "Portable package ready: $taskArchive"
Write-Host "SHA-256: $taskHash"
} finally {
    if ($taskStageOwned) { Remove-OutputItem $taskStage $taskOutputRoot }
    if ($taskCheckOwned) { Remove-OutputItem $taskCheckRoot $taskOutputRoot }
}
