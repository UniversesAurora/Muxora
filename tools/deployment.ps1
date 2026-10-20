# Shared deployment operations. All recursive mutations stay within out/.
function Assert-OutputPath([string]$Path, [string]$OutputRoot) {
    $taskRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $taskTarget = [IO.Path]::GetFullPath($Path)
    if (-not $taskTarget.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output path must be inside $taskRoot : $taskTarget"
    }
    $taskAncestor = $taskTarget
    while ($taskAncestor -and $taskAncestor.Length -ge $taskRoot.Length) {
        if (Test-Path -LiteralPath $taskAncestor) {
            if ((Get-Item -LiteralPath $taskAncestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Output path contains a link: $taskAncestor"
            }
        }
        $taskAncestor = [IO.Path]::GetDirectoryName($taskAncestor)
    }
    return $taskTarget
}
function Remove-OutputItem([string]$Path, [string]$OutputRoot) {
    $taskTarget = Assert-OutputPath $Path $OutputRoot
    if (-not (Test-Path -LiteralPath $taskTarget)) { return }
    $taskItem = Get-Item -LiteralPath $taskTarget -Force
    if ($taskItem.PSIsContainer) {
        $taskLinks = @(Get-ChildItem -LiteralPath $taskTarget -Recurse -Force |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
        if ($taskLinks.Count) { throw "Output directory contains a link: $($taskLinks[0].FullName)" }
        Remove-Item -LiteralPath $taskTarget -Recurse -Force
    } else { Remove-Item -LiteralPath $taskTarget -Force }
}
function Assert-DeploymentStopped([string]$Directory) {
    $taskExe = [IO.Path]::GetFullPath((Join-Path $Directory 'muxora.exe'))
    $taskRunning = @(Get-Process -Name muxora -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and [IO.Path]::GetFullPath($_.Path).Equals($taskExe, [StringComparison]::OrdinalIgnoreCase) })
    if ($taskRunning.Count) { throw "Deployment executable is running: $taskExe (PID $($taskRunning.Id -join ', ')). Close it before replacement." }
}
function Get-ManagedDeploymentItems([string]$Directory) {
    $taskDirectories = @('generic', 'platforms', 'styles', 'imageformats', 'iconengines', 'networkinformation', 'tls', 'licenses')
    foreach ($taskItem in Get-ChildItem -LiteralPath $Directory -Force) {
        if (($taskItem.PSIsContainer -and $taskItem.Name -in $taskDirectories) -or
            (!$taskItem.PSIsContainer -and ($taskItem.Name -in @('muxora.exe', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
                'build-info.json', 'deployment-files.json', 'opengl32sw.dll', 'D3Dcompiler_47.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') -or
                $taskItem.Name -like 'Qt6*.dll' -or $taskItem.Name -like 'libgcc_s_*.dll' -or
                # Visual C++ runtime copied app-local by an MSVC build.
                $taskItem.Name -like 'msvcp140*.dll' -or $taskItem.Name -like 'vcruntime140*.dll' -or
                $taskItem.Name -in @('concrt140.dll', 'vccorlib140.dll', 'dxcompiler.dll', 'dxil.dll')))) {
            $taskItem
        }
    }
}
function Reset-ManagedDeployment([string]$Directory, [string]$OutputRoot) {
    $taskDirectory = Assert-OutputPath $Directory $OutputRoot
    Assert-DeploymentStopped $taskDirectory
    if (!(Test-Path -LiteralPath $taskDirectory)) { New-Item -ItemType Directory -Path $taskDirectory -Force | Out-Null; return }
    # data/ and unknown host/user files are deliberately outside this inventory.
    $taskItems = @(Get-ManagedDeploymentItems $taskDirectory)
    foreach ($taskItem in $taskItems) { Assert-OutputPath $taskItem.FullName $OutputRoot | Out-Null }
    foreach ($taskItem in $taskItems) { Remove-OutputItem $taskItem.FullName $OutputRoot }
}
function Write-DeploymentInventory([string]$Directory) {
    $taskFiles = @(foreach ($taskItem in Get-ManagedDeploymentItems $Directory) {
        if ($taskItem.Name -eq 'deployment-files.json') { continue }
        if ($taskItem.PSIsContainer) {
            foreach ($taskFile in Get-ChildItem -LiteralPath $taskItem.FullName -File -Recurse) {
                [IO.Path]::GetRelativePath($Directory, $taskFile.FullName)
            }
        } else { $taskItem.Name }
    })
    @{ files = @($taskFiles | Sort-Object) } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $Directory 'deployment-files.json') -Encoding utf8NoBOM
}
