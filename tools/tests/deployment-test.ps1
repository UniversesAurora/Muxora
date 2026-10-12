[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepository 'tools/deployment.ps1')
$taskOutput = Join-Path $taskRepository 'out'
$taskFixture = Join-Path $taskOutput 'test-deployment-fixture'
$taskAssertions = 0
function Assert-Deployment([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:taskAssertions++
}
try {
    Remove-OutputItem $taskFixture $taskOutput
    $taskData = Join-Path $taskFixture 'data'
    New-Item -ItemType Directory -Path $taskData,(Join-Path $taskFixture 'licenses/Licenses'),(Join-Path $taskFixture 'platforms') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $taskData 'window-layout.json'), 'retained state')
    [IO.File]::WriteAllText((Join-Path $taskFixture 'settings.json'), 'retained settings')
    [IO.File]::WriteAllText((Join-Path $taskFixture 'custom.dll'), 'retained external file')
    foreach ($taskRelative in @('Qt6Old.dll','build-info.json','licenses/Licenses/old.txt','platforms/qwindows.dll')) {
        [IO.File]::WriteAllText((Join-Path $taskFixture $taskRelative), 'generated stale file')
    }
    Reset-ManagedDeployment $taskFixture $taskOutput
    Assert-Deployment ((Get-Content -LiteralPath (Join-Path $taskData 'window-layout.json') -Raw) -eq 'retained state') 'Data was changed.'
    Assert-Deployment (Test-Path -LiteralPath (Join-Path $taskFixture 'settings.json')) 'Unknown settings were removed.'
    Assert-Deployment (Test-Path -LiteralPath (Join-Path $taskFixture 'custom.dll')) 'Unknown library was removed.'
    Assert-Deployment (!(Test-Path -LiteralPath (Join-Path $taskFixture 'Qt6Old.dll'))) 'Old runtime remained.'
    Assert-Deployment (!(Test-Path -LiteralPath (Join-Path $taskFixture 'licenses'))) 'Nested licenses remained.'
    Assert-Deployment (!(Test-Path -LiteralPath (Join-Path $taskFixture 'platforms'))) 'Old plugins remained.'
    foreach ($taskRelative in @('muxora.exe','Qt6Core.dll','LICENSE')) { [IO.File]::WriteAllText((Join-Path $taskFixture $taskRelative), 'generated') }
    Write-DeploymentInventory $taskFixture
    $taskInventory = Get-Content -LiteralPath (Join-Path $taskFixture 'deployment-files.json') -Raw | ConvertFrom-Json
    Assert-Deployment ($taskInventory.files.Count -eq 3) 'Inventory did not match runtime files.'
    Assert-Deployment ('data/window-layout.json' -notin $taskInventory.files) 'Data leaked into the inventory.'
    Reset-ManagedDeployment $taskFixture $taskOutput
    Assert-Deployment (!(Test-Path -LiteralPath (Join-Path $taskFixture 'Qt6Core.dll'))) 'Second replacement did not clean runtime.'
    Assert-Deployment (Test-Path -LiteralPath (Join-Path $taskData 'window-layout.json')) 'Second replacement removed state.'
    $taskRejected = $false
    try { Assert-OutputPath (Join-Path $taskRepository 'src') $taskOutput | Out-Null } catch { $taskRejected = $true }
    Assert-Deployment $taskRejected 'Out-of-output path was allowed.'
    $taskRejected = $false
    try { Assert-OutputPath $taskOutput $taskOutput | Out-Null } catch { $taskRejected = $true }
    Assert-Deployment $taskRejected 'Output root itself was allowed for removal.'
    $taskReports = Join-Path $taskOutput 'test-results'
    New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
    @{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); assertions = $taskAssertions;
       repeatedReplacement = $true; userFilesPreserved = $true; runtimeInventoryChecked = $true;
       outputScopeChecked = $true } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $taskReports 'deployment-test.json') -Encoding utf8NoBOM
    Write-Host "Deployment checks passed: $taskAssertions assertions."
} finally { Remove-OutputItem $taskFixture $taskOutput }
