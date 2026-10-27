[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$SevenZip,
    [ValidateSet('Full', 'Archive')][string]$Validation = 'Full'
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
$taskBuildArguments = @{ Configuration = 'Release'; Test = ($Validation -eq 'Full'); Package = $true }
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
$taskReadme = @'
Muxora 窗口系统预览 — Windows x64 便携版

解压整个 Muxora 文件夹，然后双击 muxora.exe。无需安装 Qt 或编译器。
窗口布局和行为设置保存在相邻的 data/window-layout.json 中。
移动整个文件夹即可带走布局；请将文件夹放在可写位置。

Ctrl+Shift+P：命令面板
Ctrl+T：新建标签
Ctrl+,：窗口设置
Ctrl+Alt+T / Ctrl+Alt+B：显示或隐藏中央标签栏／侧边收纳条
Ctrl+Shift+PgUp / Ctrl+Shift+PgDown：在当前栏中前后移动标签
Alt+PgUp / Alt+PgDown：将当前窗格移到前后标签
Ctrl+Alt+方向键：标签组导航
Alt+方向键：窗格导航
Ctrl+Shift+E / Ctrl+Shift+O：向右 / 向下拆分窗格

原条带内拖动保留内容；首次离开条带后形成浮窗，Esc 恢复原布局。
投放到停靠指示或标签条时合并；单行模式中可用滚轮滚动目标条带，移开后保留滚动位置。
经过标签条或侧边收纳条时显示对应样式，并在插入位置让位；靠近两端停留可自动滚动。
设置中可分别选择普通和固定侧栏标签放在顶部或底部，也可开启多行显示。
多行拖拽按鼠标所在行插入；可选滚轮向下展开全部行、向上只显示活动行。
标签右键菜单提供选色和排序；标签栏或菜单栏右键可切换图标、新建按钮与编号。
侧栏的图标和编号独立设置，默认隐藏；未设置颜色的收纳标签保留原深紫色。
收纳标签的临时展开面板可拖动内侧边缘调整大小，每个标签分别记住，与固定尺寸独立。
右键或命令面板可设置或重置临时展开大小；完整窗口和浮窗分别记住工具条布局。
命令面板和右键菜单提供标签／窗格跨组、跨窗口移动及窗格方向交换。
标签组菜单区分移动、交换与合并，固定侧栏标签组也可参与。
Ctrl+K、Z：切换禅模式；普通窗口会自动进入设置指定的全屏范围。
设置中的“禅模式默认全屏范围”可选择窗口、标签组或窗格全屏。
首次启动使用初始布局；恢复初始布局可从命令面板操作。

本版供窗口交互试用，SSH、SFTP 和真实终端尚未接入。
编辑区文字暂不随退出保存。用户快捷键保存在 data/User/keybindings.json，界面语言保存在 data/User/settings.json。
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
            if (-not $taskProcess.WaitForExit(5000)) {
                throw 'The startup-check process did not exit after termination; scratch files may still be in use.'
            }
            throw 'Extracted portable application did not complete its startup check within 15 seconds.'
        }
        $taskOutput.GetAwaiter().GetResult() | Out-Null
        $taskDiagnostics = $taskError.GetAwaiter().GetResult()
        if ($taskProcess.ExitCode -ne 0) { throw "Portable startup failed ($($taskProcess.ExitCode)): $taskDiagnostics" }
    } finally { $taskProcess.Dispose() }
}
$taskPackageResult = @{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); archive = "$taskArchiveName.7z";
   validation = $Validation; archiveIntegrity = $true; nonAsciiPathWithSpaces = $true;
   applicationChecksExecuted = ($Validation -eq 'Full') }
if ($Validation -eq 'Full') {
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
$taskPackageResult.extractedStartupExitCode = 0
$taskPackageResult.restoredStartupExitCode = 0
$taskPackageResult.sdkRemovedFromPath = $true
$taskPackageResult.localStateSaved = $true
$taskPackageResult.layoutRestored = $true
} else {
    if (-not (Test-Path -LiteralPath $taskExecutable -PathType Leaf)) { throw 'Extracted portable executable is missing.' }
    foreach ($taskRelative in $taskInventory.files) {
        $taskExtractedFile = Assert-OutputPath ([IO.Path]::GetFullPath($taskRelative, $taskExtracted)) $taskOutputRoot
        if (-not $taskExtractedFile.StartsWith($taskExtracted + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $taskExtractedFile -PathType Leaf)) {
            throw 'Extracted archive is missing a deployment inventory file.'
        }
    }
    Write-Host 'Archive checked. Application tests and startup checks were not executed.'
}
$taskPackageResult | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $taskRepository 'out/test-results/portable-package.json') -Encoding utf8NoBOM
Write-Host "Portable package ready: $taskArchive"
} finally {
    if ($taskStageOwned) { Remove-OutputItem $taskStage $taskOutputRoot }
    if ($taskCheckOwned) { Remove-OutputItem $taskCheckRoot $taskOutputRoot }
}
