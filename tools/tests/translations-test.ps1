[CmdletBinding()]
param([string]$QtRoot, [string]$Report)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepository 'tools/toolchain.ps1')
$taskQt = Resolve-QtKit $QtRoot $taskRepository
$taskPwsh = (Get-Process -Id $PID).Path
$taskFixture = Join-Path ([IO.Path]::GetTempPath()) ('muxora-translation-tests-检查 ' + [guid]::NewGuid().ToString('N'))
$taskResults = [Collections.Generic.List[object]]::new()
$taskOriginals = @{}

function Change-Source([string]$Relative, [scriptblock]$Change) {
    $path = Join-Path $taskFixture $Relative
    $original = [IO.File]::ReadAllText($path)
    $taskOriginals[$path] = $original
    $replacement = & $Change $original
    if ($replacement -ceq $original) { throw "Mutation did not change $Relative" }
    [IO.File]::WriteAllText($path, $replacement, [Text.UTF8Encoding]::new($false))
}

function Change-Catalog([string]$Source, [scriptblock]$Change) {
    $path = Join-Path $taskFixture 'modules/window-system/translations/window_system_zh_CN.ts'
    $original = [IO.File]::ReadAllText($path)
    $taskOriginals[$path] = $original
    [xml]$xml = $original
    $message = @($xml.SelectNodes('/TS/context/message') | Where-Object {
        $_.SelectSingleNode('source').InnerText -ceq $Source
    })
    if ($message.Count -ne 1) { throw "Expected one catalog entry: $Source" }
    & $Change $message[0]
    [IO.File]::WriteAllText($path, $xml.OuterXml, [Text.UTF8Encoding]::new($false))
}

function Check-Fixture([string]$Name, [scriptblock]$Mutation, [string]$ExpectedError = '') {
    try {
        if ($Mutation) { & $Mutation }
        $resultPath = Join-Path $taskFixture 'result.json'
        $output = & $taskPwsh -NoProfile -File (Join-Path $taskRepository 'tools/check-translations.ps1') `
            -SourceRoot $taskFixture -QtRoot $taskQt -Report $resultPath 2>&1
        $exitCode = $LASTEXITCODE
        if (-not (Test-Path -LiteralPath $resultPath)) { throw "${Name}: checker did not produce a report: $output" }
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        if ($ExpectedError) {
            if ($exitCode -eq 0 -or $result.passed -or -not ($result.errors -like ('*' + $ExpectedError + '*'))) {
                throw "${Name}: expected rejection containing '$ExpectedError', got $output"
            }
        } elseif ($exitCode -ne 0 -or -not $result.passed) { throw "${Name}: valid fixture failed: $output" }
        $taskResults.Add([pscustomobject]@{ name = $Name; passed = $true; checkerExitCode = $exitCode;
            expectedError = $ExpectedError; errors = @($result.errors) })
        Write-Host "PASS: $Name"
    } finally {
        foreach ($path in @($taskOriginals.Keys)) {
            [IO.File]::WriteAllText($path, $taskOriginals[$path], [Text.UTF8Encoding]::new($false))
        }
        $taskOriginals.Clear()
    }
}

try {
    New-Item -ItemType Directory -Path $taskFixture | Out-Null
    foreach ($directory in @('modules', 'src')) {
        Copy-Item -LiteralPath (Join-Path $taskRepository $directory) -Destination $taskFixture -Recurse
    }
    New-Item -ItemType Directory -Path (Join-Path $taskFixture 'tools') | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRepository 'tools/translation-exemptions.json') -Destination (Join-Path $taskFixture 'tools')
    Check-Fixture 'valid source and catalogs' {}
    Check-Fixture 'unmarked UI literal' {
        Change-Source 'modules/window-system/src/window_manager.cpp' {
            param($text)
            $text + "`nvoid translationFixture() { new QLabel(`"Untranslated test label`"); }`n"
        }
    } 'unmarked text: Untranslated test label'
    Check-Fixture 'unmarked single-word UI literal' {
        Change-Source 'modules/window-system/src/window_manager.cpp' {
            param($text)
            $text + "`nvoid translationFixture() { new QLabel(`"settings`" /* label */); }`n"
        }
    } 'unmarked text: settings'
    Check-Fixture 'removed static-table marker' {
        Change-Source 'modules/window-system/src/commands/tab_group_commands.cpp' {
            param($text)
            $text.Replace('QT_TRANSLATE_NOOP("WindowSystem", "Name: A to Z")', '"Name: A to Z"')
        }
    } 'message no longer extracted: WindowSystem | Name: A to Z'
    Check-Fixture 'missing catalog message' {
        Change-Catalog 'Settings' { param($message); [void]$message.ParentNode.RemoveChild($message) }
    } 'missing extracted message: WindowSystem | Settings'
    Check-Fixture 'damaged placeholder' {
        Change-Catalog 'Command was not executed: %1' {
            param($message)
            $message.SelectSingleNode('translation').InnerText = '命令未执行'
        }
    } 'placeholders differ: Command was not executed: %1'
    Check-Fixture 'unfinished translation' {
        Change-Catalog 'Settings' { param($message); $message.SelectSingleNode('translation').SetAttribute('type', 'unfinished') }
    } 'missing or unfinished translation: Settings'
    Check-Fixture 'empty translation' {
        Change-Catalog 'Settings' { param($message); $message.SelectSingleNode('translation').InnerText = '' }
    } 'empty translation: Settings'
    Check-Fixture 'missing plural form' {
        Change-Catalog '%n tabs' { param($message); $translation = $message.SelectSingleNode('translation'); $translation.RemoveAll() }
    } 'incorrect plural form count: %n tabs'
    Check-Fixture 'damaged mnemonic' {
        Change-Catalog '&File' { param($message); $message.SelectSingleNode('translation').InnerText = '文件' }
    } 'mnemonics or markup differ: &File'
    Check-Fixture 'valid fixture after every mutation is undone' {}
    if ($Report) {
        [ordered]@{ passed = $true; cases = $taskResults.ToArray() } | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $Report -Encoding utf8NoBOM
    }
    Write-Host "Translation checker regression passed: $($taskResults.Count) cases."
} finally {
    $resolved = [IO.Path]::GetFullPath($taskFixture)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'muxora-translation-tests-*') { throw 'Refusing to remove an unexpected translation test directory.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
