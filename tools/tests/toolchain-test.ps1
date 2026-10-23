[CmdletBinding()]
param()
# Checks build-directory reuse decisions without a compiler: a cache for another
# compiler, or a /showIncludes prefix recorded in another code page, is stale.
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepository 'tools/deployment.ps1')
. (Join-Path $taskRepository 'tools/toolchain.ps1')
$taskOutput = Join-Path $taskRepository 'out'
$taskFixture = Join-Path $taskOutput 'test-toolchain-fixture'
$taskAssertions = 0
function Assert-Toolchain([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:taskAssertions++
}
function Write-Fixture([string]$Compiler, [byte[]]$Prefix) {
    New-Item -ItemType Directory -Path (Join-Path $taskFixture 'CMakeFiles') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $taskFixture 'CMakeCache.txt'), "CMAKE_CXX_COMPILER:FILEPATH=$Compiler`n")
    $taskRules = [Collections.Generic.List[byte]]::new()
    $taskRules.AddRange([Text.Encoding]::ASCII.GetBytes("# rules`nmsvc_deps_prefix = "))
    if ($Prefix) { $taskRules.AddRange($Prefix) }
    $taskRules.AddRange([Text.Encoding]::ASCII.GetBytes("  `n`nrule CXX_COMPILER`n"))
    [IO.File]::WriteAllBytes((Join-Path $taskFixture 'CMakeFiles/rules.ninja'), $taskRules.ToArray())
}
try {
    Remove-OutputItem $taskFixture $taskOutput
    $taskCompiler = 'C:\Tools\MSVC\bin\Hostx64\x64\cl.exe'
    $taskNote = '注意: 包含文件:'
    $taskToolchain = [pscustomobject]@{ Compiler = $taskCompiler; ShowIncludesPrefix = $taskNote }
    Assert-Toolchain ($null -eq (Get-StaleBuildReason $taskToolchain $taskFixture)) 'A missing build directory was stale.'

    Write-Fixture $taskCompiler ([Text.Encoding]::UTF8.GetBytes($taskNote))
    Assert-Toolchain ($null -eq (Get-StaleBuildReason $taskToolchain $taskFixture)) 'A matching build directory was stale.'

    Write-Fixture $taskCompiler ([Text.Encoding]::GetEncoding(936).GetBytes($taskNote))
    Assert-Toolchain ((Get-StaleBuildReason $taskToolchain $taskFixture) -match 'another code page') 'A GBK prefix was reused.'

    Write-Fixture $taskCompiler ([Text.Encoding]::ASCII.GetBytes('Note: including file:'))
    Assert-Toolchain ((Get-StaleBuildReason $taskToolchain $taskFixture) -match 'prefix changed') 'A prefix in another language was reused.'

    Write-Fixture 'C:\Other\cl.exe' ([Text.Encoding]::UTF8.GetBytes($taskNote))
    Assert-Toolchain ((Get-StaleBuildReason $taskToolchain $taskFixture) -match 'compiler changed') 'Another compiler was reused.'
} finally {
    Remove-OutputItem $taskFixture $taskOutput
}
Write-Host "Toolchain checks passed: $taskAssertions assertions."
