<#
.SYNOPSIS
Checks the repository's Markdown documents before a commit.

.DESCRIPTION
Reports, and exits with 1 when any is found:
- relative links whose file or heading anchor does not exist;
- documents that cannot be reached by following links from README.md or
  AGENTS.md (the documentation index is reached from README.md);
- absolute Windows paths with concrete directory names, which would expose
  a machine's local layout in this public repository.
The rules behind these checks are in docs/development/documentation.md.

.EXAMPLE
pwsh tools/check-docs.ps1
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent $PSScriptRoot
Push-Location $taskRepository
try {
    $taskFiles = @(git ls-files --cached --others --exclude-standard -- '*.md' | Where-Object { Test-Path -LiteralPath $_ })
    $taskKnown = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $taskFiles) { [void]$taskKnown.Add($file) }
    # Not part of the documentation set: the Claude bridge file and license texts.
    $taskUnindexed = @('CLAUDE.md')
    $taskUnindexedPrefixes = @('third_party/licenses/')
    $taskProblems = [Collections.Generic.List[string]]::new()
    $taskAnchors = @{}

    function Get-Slug([string]$Heading) {
        $builder = [Text.StringBuilder]::new()
        foreach ($character in $Heading.Trim().ToLowerInvariant().ToCharArray()) {
            $category = [int][Globalization.CharUnicodeInfo]::GetUnicodeCategory($character)
            if ($character -eq ' ') { [void]$builder.Append('-') }
            elseif ($character -eq '-' -or $character -eq '_') { [void]$builder.Append($character) }
            elseif ($category -le 4 -or ($category -ge 8 -and $category -le 10)) { [void]$builder.Append($character) }
        }
        $builder.ToString()
    }

    function Get-Anchors([string]$File) {
        if ($taskAnchors.ContainsKey($File)) { return $taskAnchors[$File] }
        $set = [Collections.Generic.HashSet[string]]::new()
        $counts = @{}
        $fence = $false
        foreach ($line in [IO.File]::ReadAllLines((Resolve-Path -LiteralPath $File))) {
            if ($line.StartsWith('```')) { $fence = -not $fence; continue }
            if ($fence) { continue }
            $match = [regex]::Match($line, '^#{1,6}\s+(.*)$')
            if (-not $match.Success) { continue }
            $slug = Get-Slug ($match.Groups[1].Value -replace '`', '')
            $count = if ($counts.ContainsKey($slug)) { $counts[$slug] } else { 0 }
            $counts[$slug] = $count + 1
            [void]$set.Add($(if ($count -eq 0) { $slug } else { "$slug-$count" }))
        }
        $taskAnchors[$File] = $set
        $set
    }

    function Resolve-RepositoryPath([string]$From, [string]$Target) {
        $base = Split-Path -Parent $From
        $full = [IO.Path]::GetFullPath((Join-Path (Join-Path $taskRepository $base) $Target))
        $full.Substring($taskRepository.Length).TrimStart('\', '/').Replace('\', '/')
    }

    $taskLinks = @{}
    $linkPattern = [regex]'\]\((?!https?:|mailto:)([^)\s]+)\)'
    $pathPattern = [regex]'\b[A-Za-z]:[\\/](?!Users[\\/]u\b)[A-Za-z0-9_.-]+[\\/][A-Za-z0-9_.-]+'
    foreach ($file in $taskFiles) {
        $text = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $file))
        $targets = [Collections.Generic.List[string]]::new()
        foreach ($match in $linkPattern.Matches($text)) {
            $parts = $match.Groups[1].Value.Split('#', 2)
            $target = if ($parts[0]) { Resolve-RepositoryPath $file ([Uri]::UnescapeDataString($parts[0])) } else { $file }
            if (-not (Test-Path -LiteralPath (Join-Path $taskRepository $target))) {
                $taskProblems.Add("missing link target: $file -> $($match.Groups[1].Value)")
                continue
            }
            if ($taskKnown.Contains($target)) { $targets.Add($target) }
            if ($parts.Count -eq 2 -and $parts[1] -and $target.EndsWith('.md')) {
                if (-not (Get-Anchors $target).Contains([Uri]::UnescapeDataString($parts[1]))) {
                    $taskProblems.Add("missing anchor: $file -> $($match.Groups[1].Value)")
                }
            }
        }
        $taskLinks[$file] = $targets
        foreach ($match in $pathPattern.Matches($text)) {
            $taskProblems.Add("machine path: $file -> $($match.Value)")
        }
    }

    $reached = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [Collections.Generic.Queue[string]]::new()
    foreach ($root in @('README.md', 'AGENTS.md')) { $queue.Enqueue($root) }
    while ($queue.Count) {
        $file = $queue.Dequeue()
        if (-not $reached.Add($file)) { continue }
        foreach ($target in $taskLinks[$file]) { if (-not $reached.Contains($target)) { $queue.Enqueue($target) } }
    }
    foreach ($file in $taskFiles) {
        if ($reached.Contains($file) -or $file -in $taskUnindexed) { continue }
        if ($taskUnindexedPrefixes | Where-Object { $file.StartsWith($_) }) { continue }
        $taskProblems.Add("not reachable from README.md or AGENTS.md: $file")
    }

    if ($taskProblems.Count) {
        $taskProblems | ForEach-Object { Write-Output $_ }
        Write-Output "Documentation check failed: $($taskProblems.Count) problem(s) in $($taskFiles.Count) files."
        exit 1
    }
    Write-Output "Documentation check passed: $($taskFiles.Count) files."
} finally { Pop-Location }
