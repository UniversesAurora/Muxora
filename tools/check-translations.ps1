[CmdletBinding()]
param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$QtRoot,
    # Qt's lupdate; CTest passes the build's own, so no kit lookup is needed.
    [string]$Lupdate,
    [ValidateSet('All', 'WindowSystem', 'LuaRuntime', 'Muxora')][string]$Catalog = 'All',
    [string]$Report
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRepository = [IO.Path]::GetFullPath($SourceRoot)
$taskLupdate = if ($Lupdate) { $Lupdate } else {
    . (Join-Path $PSScriptRoot 'toolchain.ps1')
    Join-Path (Resolve-QtKit $QtRoot (Split-Path -Parent $PSScriptRoot)) ('bin/lupdate' + $(if ($IsWindows) { '.exe' } else { '' }))
}
if (-not (Test-Path -LiteralPath $taskLupdate -PathType Leaf)) { throw "lupdate was not found: $taskLupdate" }
$taskTemporary = Join-Path ([IO.Path]::GetTempPath()) ('muxora-translations-' + [guid]::NewGuid().ToString('N'))
$taskErrors = [Collections.Generic.List[string]]::new()
$taskGroups = @(
    @{ Name = 'WindowSystem'; Stem = 'window_system'; Directory = 'modules/window-system/translations';
       Roots = @('modules/window-system/include', 'modules/window-system/src',
                 'modules/window-system-lua/include', 'modules/window-system-lua/src') },
    @{ Name = 'LuaRuntime'; Stem = 'lua_runtime'; Directory = 'modules/lua-runtime/translations';
       Roots = @('modules/lua-runtime/include', 'modules/lua-runtime/src') },
    @{ Name = 'Muxora'; Stem = 'muxora'; Directory = 'src/app/translations';
       Roots = @('src/app', 'src/workspace_host') }
)
$taskExemptions = @(Get-Content -LiteralPath (Join-Path $taskRepository 'tools/translation-exemptions.json') -Raw | ConvertFrom-Json)
$taskUsedExemptions = @{}
$taskCounts = [Collections.Generic.List[object]]::new()
$taskCandidates = [Collections.Generic.List[object]]::new()

function Read-Source([string]$Path) {
    $encoding = [Text.UTF8Encoding]::new($false, $true)
    return [IO.File]::ReadAllText($Path, $encoding)
}

function Decode-Literal([string]$Text) {
    return [regex]::Replace($Text, '\\(?:U[0-9a-fA-F]{8}|u[0-9a-fA-F]{4}|x[0-9a-fA-F]+|[0-7]{1,3}|.)', {
        param($match)
        $escaped = $match.Value.Substring(1)
        switch -CaseSensitive ($escaped) {
            'n' { return "`n" }; 'r' { return "`r" }; 't' { return "`t" }
            'a' { return [string][char]7 }; 'b' { return [string][char]8 }
            'f' { return [string][char]12 }; 'v' { return [string][char]11 }
            default {
                if ($escaped -match '^[uUx]') { return [char]::ConvertFromUtf32([Convert]::ToInt32($escaped.Substring(1), 16)) }
                if ($escaped -match '^[0-7]+$') { return [string][char][Convert]::ToInt32($escaped, 8) }
                return $escaped
            }
        }
    })
}

function Get-Literals([string]$Source, [switch]$Lua) {
    # Comments and character literals are tokens too: quotes inside them must
    # never become strings. Adjacent C++ strings may have comments between them.
    $pattern = if ($Lua) {
        '(?<comment>--\[(?<level>=*)\[[\s\S]*?\]\k<level>\]|--[^\r\n]*)|(?<luaRaw>\[(?<rawLevel>=*)\[(?<luaText>[\s\S]*?)\]\k<rawLevel>\])|(?<string>"(?:\\[\s\S]|[^"\\])*"|''(?:\\[\s\S]|[^''\\])*'')'
    } else {
        '(?<comment>//[^\r\n]*|/\*[\s\S]*?\*/)|(?<raw>(?:u8|u|U|L)?R"(?<delimiter>[^ ()\\\t\r\n]{0,16})\((?<rawText>[\s\S]*?)\)\k<delimiter>")|(?<string>(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*")|(?<character>(?:u8|u|U|L)?''(?:\\[\s\S]|[^''\\])*'')'
    }
    $items = [Collections.Generic.List[object]]::new()
    $lastEnd = -1
    foreach ($match in [regex]::Matches($Source, $pattern)) {
        if ($match.Groups['comment'].Success) { continue }
        if (-not $Lua -and $match.Groups['character'].Success) { $lastEnd = -1; continue }
        $raw = -not $Lua -and $match.Groups['raw'].Success
        $value = if ($raw) { $match.Groups['rawText'].Value }
            elseif ($Lua -and $match.Groups['luaRaw'].Success) { $match.Groups['luaText'].Value }
            else {
                $quoted = $match.Groups['string'].Value
                $quoteAt = if ($Lua) { 0 } else { $quoted.IndexOf('"') }
                Decode-Literal $quoted.Substring($quoteAt + 1, $quoted.Length - $quoteAt - 2)
            }
        $gap = if ($lastEnd -ge 0) { $Source.Substring($lastEnd, $match.Index - $lastEnd) } else { '-' }
        $gap = [regex]::Replace($gap, '//[^\r\n]*|/\*[\s\S]*?\*/', '')
        if (-not $Lua -and $items.Count -and [string]::IsNullOrWhiteSpace($gap)) {
            $items[$items.Count - 1].Text += $value
            $items[$items.Count - 1].End = $match.Index + $match.Length
        } else {
            $items.Add([pscustomobject]@{ Text = $value; Start = $match.Index; End = $match.Index + $match.Length;
                Raw = $raw; Delimiter = $(if ($raw) { $match.Groups['delimiter'].Value } else { '' }) })
        }
        $lastEnd = $match.Index + $match.Length
    }
    return $items.ToArray()
}

function Test-Candidate([string]$Value, [string]$Prefix) {
    if ($Prefix -match '(?:translate|QT_TRANSLATE_(?:N_)?NOOP)\(\s*$') { return $false }
    if ($Prefix -match '(?:translate|QT_TRANSLATE_NOOP3)\(\s*"(?:\\.|[^"\\])*"\s*,\s*"(?:\\.|[^"\\])*"\s*,\s*$') { return $false }
    if ($Value -match '\p{IsCJKUnifiedIdeographs}' -or $Value -match '(?s)[A-Za-z][^\n]*\s[^\n]*[A-Za-z]') { return $true }
    if (($Value -replace '&', '') -cmatch '^[A-Z][a-z]+[.!?…]?$') { return $true }
    return $Value -match '[A-Za-z]' -and $Prefix -match '(?:new\s+Q(?:Label|PushButton|CheckBox)|setText|setTitle|setWindowTitle|setToolTip|setPlaceholderText|setAccessibleName|addAction|addItem)\(\s*(?:QStringLiteral\(\s*)?$'
}

function Test-Source([string]$Relative, [string]$Source, [bool]$Lua = $false, [int]$LineOffset = 0) {
    foreach ($literal in @(Get-Literals $Source -Lua:$Lua)) {
        $prefix = $Source.Substring([Math]::Max(0, $literal.Start - 200), [Math]::Min(200, $literal.Start))
        if ($literal.Raw -and $literal.Delimiter -eq 'lua') {
            $before = $Source.Substring(0, $literal.Start)
            Test-Source $Relative $literal.Text $true ($LineOffset + ($before.Split("`n").Count - 1))
            continue
        }
        $marked = $prefix -match '(?:translate|QT_TRANSLATE_(?:N_)?NOOP)\(\s*"[^"\r\n]+"\s*,\s*$'
        if ($marked) {
            if ($literal.Text -match '\p{IsCJKUnifiedIdeographs}') { $taskErrors.Add("$Relative contains a non-English translation source: $($literal.Text)") }
            continue
        }
        $value = if ($literal.Raw -and $literal.Delimiter -eq 'qss') { '<QSS stylesheet>' } else { $literal.Text }
        if (-not (Test-Candidate $value $prefix)) { continue }
        $taskCandidates.Add([pscustomobject]@{ file = $Relative; text = $value })
        $key = $Relative + "`n" + $value
        $exemption = @($taskExemptions | Where-Object { $_.file -ceq $Relative -and $_.text -ceq $value })
        if ($exemption.Count -eq 1 -and -not [string]::IsNullOrWhiteSpace($exemption[0].reason)) {
            $taskUsedExemptions[$key] = 1 + $taskUsedExemptions[$key]
        } else {
            $line = $LineOffset + $Source.Substring(0, $literal.Start).Split("`n").Count
            $summary = ($value -replace '\s+', ' ').Trim()
            if ($summary.Length -gt 140) { $summary = $summary.Substring(0, 140) + '…' }
            $taskErrors.Add("${Relative}:${line}: unmarked text: $summary")
        }
    }
}

function Message-Key($Message, [string]$Context) {
    $comment = $Message.SelectSingleNode('comment')
    return $Context + "`n" + $Message.SelectSingleNode('source').InnerText + "`n" +
        $(if ($comment) { $comment.InnerText } else { '' }) + "`n" + $Message.GetAttribute('numerus')
}

function Placeholders([string]$Text) {
    return (@([regex]::Matches($Text, '%(?:L?(?:[1-9][0-9]?|n))') | ForEach-Object Value | Sort-Object) -join '|')
}

function Structure([string]$Text) {
    $tags = @([regex]::Matches($Text, '</?[A-Za-z][A-Za-z0-9]*(?:\s[^<>]*?)?/?>') | ForEach-Object Value)
    return (($Text.Split("`n").Count - 1).ToString() + '|' +
        ([regex]::Matches(($Text -replace '&&', ''), '&').Count).ToString() + '|' + ($tags -join '|'))
}

try {
    New-Item -ItemType Directory -Path $taskTemporary | Out-Null
    foreach ($group in $taskGroups) {
        if ($Catalog -ne 'All' -and $Catalog -ne $group.Name) { continue }
        $sources = @($group.Roots | ForEach-Object {
            $directory = Join-Path $taskRepository $_
            if (-not (Test-Path -LiteralPath $directory)) { throw "Missing translation source directory: $_" }
            Get-ChildItem -LiteralPath $directory -Recurse -File | Where-Object {
                $_.Extension -in '.cpp', '.hpp', '.h', '.ui', '.qml', '.lua', '.json', '.html', '.qrc', '.in' -and
                $_.FullName -notmatch '[\\/](?:tests|translations|benchmarks)[\\/]'
            }
        } | Sort-Object FullName -Unique)
        if (-not $sources.Count) { throw "No sources found for $($group.Name)" }
        foreach ($file in $sources) {
            $relative = [IO.Path]::GetRelativePath($taskRepository, $file.FullName).Replace('\', '/')
            if ($file.Extension -in '.cpp', '.hpp', '.h', '.lua') {
                Test-Source $relative (Read-Source $file.FullName) ($file.Extension -eq '.lua')
            }
        }
        $fresh = Join-Path $taskTemporary ($group.Stem + '.ts')
        # Qt reads @list files in the local code page. Its JSON project input
        # keeps Unicode source paths intact and avoids command-line length limits.
        $projectFile = Join-Path $taskTemporary ($group.Stem + '.json')
        ConvertTo-Json -InputObject @(@{ projectFile = (Join-Path $taskRepository 'CMakeLists.txt');
            sources = @($sources.FullName) }) -Depth 4 |
            Set-Content -LiteralPath $projectFile -Encoding utf8NoBOM
        $extraction = & $taskLupdate -project $projectFile -no-obsolete -locations none -source-language en -target-language en -ts $fresh 2>&1
        if ($LASTEXITCODE -ne 0) { throw "lupdate failed for $($group.Name): $extraction" }
        [xml]$extracted = Read-Source $fresh
        $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($context in $extracted.SelectNodes('/TS/context')) {
            foreach ($message in $context.SelectNodes('message')) { [void]$expected.Add((Message-Key $message $context.name)) }
        }
        foreach ($language in @('en', 'zh_CN')) {
            $path = $group.Directory + '/' + $group.Stem + '_' + $language + '.ts'
            [xml]$document = Read-Source (Join-Path $taskRepository $path)
            if ($document.DocumentElement.GetAttribute('sourcelanguage') -cne 'en' -or
                $document.DocumentElement.GetAttribute('language') -cne $language) { $taskErrors.Add("${path}: incorrect source or target language") }
            $actual = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            foreach ($context in $document.SelectNodes('/TS/context')) {
                foreach ($message in $context.SelectNodes('message')) {
                    $key = Message-Key $message $context.name
                    $source = $message.SelectSingleNode('source').InnerText
                    if (-not $actual.Add($key)) { $taskErrors.Add("${path}: duplicate message: $source") }
                    if ($source -match '\p{IsCJKUnifiedIdeographs}') { $taskErrors.Add("${path}: non-English source: $source") }
                    $translation = $message.SelectSingleNode('translation')
                    if (-not $translation -or $translation.GetAttribute('type')) { $taskErrors.Add("${path}: missing or unfinished translation: $source"); continue }
                    $plural = $message.GetAttribute('numerus') -eq 'yes'
                    $forms = @(if ($plural) { $translation.SelectNodes('numerusform') } else { $translation })
                    $count = if ($plural -and $language -eq 'en') { 2 } else { 1 }
                    if ($forms.Count -ne $count) { $taskErrors.Add("${path}: incorrect plural form count: $source") }
                    foreach ($form in $forms) {
                        $text = $form.InnerText
                        if ([string]::IsNullOrWhiteSpace($text)) { $taskErrors.Add("${path}: empty translation: $source") }
                        if ((Placeholders $text) -cne (Placeholders $source)) { $taskErrors.Add("${path}: placeholders differ: $source") }
                        if ((Structure $text) -cne (Structure $source)) { $taskErrors.Add("${path}: line breaks, mnemonics or markup differ: $source") }
                    }
                }
            }
            foreach ($key in $expected) { if (-not $actual.Contains($key)) { $taskErrors.Add("${path}: missing extracted message: $($key.Replace("`n", ' | '))") } }
            foreach ($key in $actual) { if (-not $expected.Contains($key)) { $taskErrors.Add("${path}: message no longer extracted: $($key.Replace("`n", ' | '))") } }
            $taskCounts.Add([pscustomobject]@{ catalog = $path; sourceFiles = $sources.Count; extracted = $expected.Count; messages = $actual.Count })
        }
    }
    foreach ($exemption in $taskExemptions) {
        if (-not ($taskGroups | Where-Object { ($Catalog -eq 'All' -or $_.Name -eq $Catalog) -and
            ($_.Roots | Where-Object { $exemption.file.StartsWith($_ + '/', [StringComparison]::Ordinal) }) })) { continue }
        $key = $exemption.file + "`n" + $exemption.text
        $used = if ($taskUsedExemptions.ContainsKey($key)) { $taskUsedExemptions[$key] } else { 0 }
        if ($used -ne $exemption.count) { $taskErrors.Add("$($exemption.file): exemption count changed ($used/$($exemption.count)): $($exemption.text)") }
    }
    $result = [ordered]@{ passed = $taskErrors.Count -eq 0; catalogs = $taskCounts.ToArray(); candidates = $taskCandidates.ToArray(); errors = $taskErrors.ToArray() }
    if ($Report) { $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Report -Encoding utf8NoBOM }
    if ($taskErrors.Count) { $taskErrors | ForEach-Object { Write-Host $_ }; throw "Translation check failed with $($taskErrors.Count) issue(s)." }
    Write-Host "Translation check passed: $($taskCounts.Count) catalogs; source extraction, translations and candidate scan."
} finally {
    $resolved = [IO.Path]::GetFullPath($taskTemporary)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'muxora-translations-*') { throw 'Refusing to remove an unexpected translation scratch directory.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
