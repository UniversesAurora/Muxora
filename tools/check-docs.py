#!/usr/bin/env python3
"""Checks the repository's Markdown documents before a commit.

Reports, and exits with 1 when any is found:
- relative links whose file or heading anchor does not exist;
- documents that cannot be reached by following links from README.md or
  AGENTS.md (the documentation index is reached from README.md);
- absolute Windows paths with concrete directory names, which would expose
  a machine's local layout in this public repository.
The rules behind these checks are in docs/development/documentation.md.

Usage: python tools/check-docs.py
"""
import os
import pathlib
import re
import subprocess
import sys
import unicodedata
import urllib.parse
from collections import deque

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent
# Not part of the documentation set: the Claude bridge file and license texts.
UNINDEXED = {'claude.md'}
UNINDEXED_PREFIXES = ('third_party/licenses/',)
LINK = re.compile(r'\]\((?!https?:|mailto:)([^)\s]+)\)')
MACHINE_PATH = re.compile(r'\b[A-Za-z]:[\\/](?!Users[\\/]u\b)[A-Za-z0-9_.-]+[\\/][A-Za-z0-9_.-]+')
HEADING = re.compile(r'^#{1,6}\s+(.*)$')
SLUG_CATEGORIES = {'Lu', 'Ll', 'Lt', 'Lm', 'Lo', 'Nd', 'Nl', 'No'}


def read(relative):
    with open(REPOSITORY / relative, encoding='utf-8-sig') as file:
        return file.read()


def slug(heading):
    # GitHub's anchors: lower case, spaces become hyphens, and only letters,
    # digits, hyphens and underscores are kept.
    kept = []
    for character in heading.strip().lower():
        if character == ' ':
            kept.append('-')
        elif character in '-_' or unicodedata.category(character) in SLUG_CATEGORIES:
            kept.append(character)
    return ''.join(kept)


anchor_cache = {}


def anchors(relative):
    if relative.lower() in anchor_cache:
        return anchor_cache[relative.lower()]
    found, counts, fence = set(), {}, False
    for line in read(relative).splitlines():
        if line.startswith('```'):
            fence = not fence
            continue
        match = None if fence else HEADING.match(line)
        if not match:
            continue
        name = slug(match.group(1).replace('`', ''))
        count = counts.get(name, 0)
        counts[name] = count + 1
        found.add(name if count == 0 else f'{name}-{count}')
    anchor_cache[relative.lower()] = found
    return found


def resolve(origin, target):
    full = os.path.normpath(os.path.join(REPOSITORY, os.path.dirname(origin), target))
    return os.path.relpath(full, REPOSITORY).replace(os.sep, '/')


def main():
    listed = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '--', '*.md'],
                            cwd=REPOSITORY, capture_output=True, text=True, encoding='utf-8', check=True).stdout
    files = [name for name in listed.splitlines() if (REPOSITORY / name).is_file()]
    known = {name.lower() for name in files}
    problems, links = [], {}

    for name in files:
        text = read(name)
        targets = []
        for match in LINK.finditer(text):
            path, _, fragment = match.group(1).partition('#')
            target = resolve(name, urllib.parse.unquote(path)) if path else name
            if not (REPOSITORY / target).exists():
                problems.append(f'missing link target: {name} -> {match.group(1)}')
                continue
            if target.lower() in known:
                targets.append(target.lower())
            if fragment and target.endswith('.md') and urllib.parse.unquote(fragment) not in anchors(target):
                problems.append(f'missing anchor: {name} -> {match.group(1)}')
        links[name.lower()] = targets
        for match in MACHINE_PATH.finditer(text):
            problems.append(f'machine path: {name} -> {match.group(0)}')

    reached, queue = set(), deque(['readme.md', 'agents.md'])
    while queue:
        name = queue.popleft()
        if name in reached:
            continue
        reached.add(name)
        queue.extend(target for target in links.get(name, []) if target not in reached)
    for name in files:
        if name.lower() in reached or name.lower() in UNINDEXED or name.startswith(UNINDEXED_PREFIXES):
            continue
        problems.append(f'not reachable from README.md or AGENTS.md: {name}')

    if problems:
        for problem in problems:
            print(problem)
        print(f'Documentation check failed: {len(problems)} problem(s) in {len(files)} files.')
        return 1
    print(f'Documentation check passed: {len(files)} files.')
    return 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.exit(main())
