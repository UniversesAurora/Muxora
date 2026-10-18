[CmdletBinding()]
param(
    [string]$BuildDirectory = 'out/build/dev-release',
    [Parameter(Mandatory)][string]$Output
)
# Counts actual Qt Test rows (function + data tag, excluding initTestCase and
# cleanupTestCase) from the *-results.txt files that CTest suites write with
# "-o <name>-results.txt,txt". A row that reports FAIL! or XPASS is failed even
# when it also printed PASS; repeated FAIL! lines for one row count once.
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
$taskBuild = [IO.Path]::GetFullPath($BuildDirectory, $taskRepository)
$taskOutput = [IO.Path]::GetFullPath($Output, $taskRepository)
if (-not (Test-Path -LiteralPath $taskBuild -PathType Container)) { throw "Build directory not found: $taskBuild" }

# The data tag ends at the ')' that is followed by whitespace or the end of
# the line, so failure messages such as "returned FALSE. ()" stay out of it.
$taskRowPattern = [regex]'^(PASS|FAIL!|SKIP|BLACKLIST|XFAIL|XPASS)\s*:\s*(\S+?)::(\w+)(\((.*?)\)(?=\s|$))?'
$taskFixtureFunctions = @('initTestCase', 'cleanupTestCase')

function Get-NormalizedPath([string]$Path) { [IO.Path]::GetFullPath($Path).Replace('\', '/').ToLowerInvariant() }

# Registered suites and the result file each one writes, read from CTest's
# generated CTestTestfile.cmake files.
function Get-RegisteredSuites([string]$Root) {
    $suites = [ordered]@{}
    $addTest = [regex]'add_test\(\[=\[(?<name>[^\]]+)\]=\]\s+(?<args>.*)\)\s*$'
    $resultArgument = [regex]'"(?<path>[^"]+-results\.txt),txt"'
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -Filter 'CTestTestfile.cmake' -File) {
        foreach ($line in Get-Content -LiteralPath $file.FullName -Encoding utf8) {
            $match = $addTest.Match($line)
            if (-not $match.Success) { continue }
            $result = $resultArgument.Match($match.Groups['args'].Value)
            $path = if ($result.Success) { $result.Groups['path'].Value } else { $null }
            $suites[$match.Groups['name'].Value] = $path
        }
    }
    $suites
}

function Read-SuiteRows([string]$Path) {
    $rows = [ordered]@{}
    $fixtureFailures = [Collections.Generic.List[string]]::new()
    foreach ($line in Get-Content -LiteralPath $Path -Encoding utf8) {
        $match = $taskRowPattern.Match($line)
        if (-not $match.Success) { continue }
        $status = $match.Groups[1].Value
        $function = $match.Groups[3].Value
        $tag = if ($match.Groups[4].Success) { $match.Groups[5].Value } else { '' }
        if ($taskFixtureFunctions -contains $function) {
            if ($status -in @('FAIL!', 'XPASS')) { $fixtureFailures.Add($line.Trim()) }
            continue
        }
        $key = if ($tag) { "$function($tag)" } else { $function }
        if (-not $rows.Contains($key)) { $rows[$key] = [Collections.Generic.HashSet[string]]::new() }
        [void]$rows[$key].Add($status)
    }
    [pscustomobject]@{ Rows = $rows; FixtureFailures = $fixtureFailures }
}

function Get-RowOutcome([Collections.Generic.HashSet[string]]$Statuses) {
    if ($Statuses.Contains('FAIL!') -or $Statuses.Contains('XPASS')) { return 'failed' }
    if ($Statuses.Contains('PASS') -or $Statuses.Contains('XFAIL')) { return 'passed' }
    'skipped'
}

$taskRegistered = Get-RegisteredSuites $taskBuild
$taskFiles = @{}
foreach ($file in Get-ChildItem -LiteralPath $taskBuild -Recurse -Filter '*-results.txt' -File) {
    $taskFiles[(Get-NormalizedPath $file.FullName)] = $file
}
# Every registered suite appears once; result files that no registered suite
# writes (for example from a removed suite) are still reported by path.
$taskSuites = [Collections.Generic.List[object]]::new()
$taskClaimed = [Collections.Generic.HashSet[string]]::new()
foreach ($name in $taskRegistered.Keys) {
    $path = $taskRegistered[$name]
    $file = if ($path) { $taskFiles[(Get-NormalizedPath $path)] } else { $null }
    if ($file) { [void]$taskClaimed.Add((Get-NormalizedPath $file.FullName)) }
    $taskSuites.Add([pscustomobject]@{ Name = $name; File = $file; ExpectedPath = $path })
}
foreach ($key in ($taskFiles.Keys | Sort-Object)) {
    if ($taskClaimed.Contains($key)) { continue }
    $file = $taskFiles[$key]
    $taskSuites.Add([pscustomobject]@{ Name = [IO.Path]::GetRelativePath($taskBuild, $file.FullName).Replace('\', '/'); File = $file; ExpectedPath = $file.FullName })
}

$taskSuiteReports = [Collections.Generic.List[object]]::new()
$taskNoResults = [Collections.Generic.List[object]]::new()
$taskTotals = [ordered]@{ suites = $taskSuites.Count; suitesWithResults = 0; passed = 0; failed = 0; skipped = 0 }
foreach ($suite in $taskSuites) {
    if (-not $suite.File -or $suite.File.Length -eq 0) {
        $taskNoResults.Add([ordered]@{ name = $suite.Name; file = $suite.ExpectedPath })
        continue
    }
    $parsed = Read-SuiteRows $suite.File.FullName
    $counts = @{ passed = 0; failed = 0; skipped = 0 }
    $failedRows = [Collections.Generic.List[string]]::new()
    foreach ($key in $parsed.Rows.Keys) {
        $outcome = Get-RowOutcome $parsed.Rows[$key]
        $counts[$outcome]++
        if ($outcome -eq 'failed') { $failedRows.Add($key) }
    }
    if ($parsed.Rows.Count -eq 0 -and $parsed.FixtureFailures.Count -eq 0) {
        $taskNoResults.Add([ordered]@{ name = $suite.Name; file = $suite.File.FullName })
        continue
    }
    $taskTotals.suitesWithResults++
    foreach ($outcome in @('passed', 'failed', 'skipped')) { $taskTotals[$outcome] += $counts[$outcome] }
    $taskSuiteReports.Add([ordered]@{
        name = $suite.Name
        file = $suite.File.FullName
        modified = $suite.File.LastWriteTime.ToString('o')
        passed = $counts.passed
        failed = $counts.failed
        skipped = $counts.skipped
        failedRows = @($failedRows)
        fixtureFailures = @($parsed.FixtureFailures)
    })
}
$taskTotals.noResult = $taskNoResults.Count

$taskReport = [ordered]@{
    buildDirectory = $taskBuild
    generated = (Get-Date).ToString('o')
    totals = $taskTotals
    suites = @($taskSuiteReports)
    noResult = @($taskNoResults)
}
New-Item -ItemType Directory -Path (Split-Path -Parent $taskOutput) -Force | Out-Null
$taskReport | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskOutput -Encoding utf8NoBOM
Write-Host ("{0} suites ({1} with results, {2} without): {3} passed, {4} failed, {5} skipped rows -> {6}" -f
    $taskTotals.suites, $taskTotals.suitesWithResults, $taskTotals.noResult,
    $taskTotals.passed, $taskTotals.failed, $taskTotals.skipped, $taskOutput)
