#!/usr/bin/env python3
"""Checks that the translation catalogs cover every first-party source.

Extracts each catalog group again with lupdate and compares the result with the
committed catalogs: source and target language, missing and stale messages,
unfinished or empty translations, placeholders, plural forms, line breaks,
mnemonics and markup. Running lupdate over each committed catalog must leave it
byte for byte unchanged, so catalogs are only ever written by the lupdate
build targets, never formatted by hand. It also scans the sources for text that looks like UI
prose but is not marked for translation; tools/translation-exemptions.json
lists the known exceptions with their exact counts.

Usage: python tools/check-translations.py [--lupdate PATH | --qt-root DIR]
CTest runs it as translations.coverage with the build's own lupdate.
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree

GROUPS = [
    {'name': 'WindowSystem', 'stem': 'window_system', 'directory': 'modules/window-system/translations',
     'target': 'window_system_lupdate',
     'roots': ['modules/window-system/include', 'modules/window-system/src',
               'modules/window-system-lua/include', 'modules/window-system-lua/src']},
    {'name': 'LuaRuntime', 'stem': 'lua_runtime', 'directory': 'modules/lua-runtime/translations',
     'target': 'lua_runtime_lupdate',
     'roots': ['modules/lua-runtime/include', 'modules/lua-runtime/src']},
    {'name': 'Muxora', 'stem': 'muxora', 'directory': 'src/app/translations',
     'target': 'muxora_lupdate',
     'roots': ['src/app', 'src/workspace_host']},
]
EXTENSIONS = {'.cpp', '.hpp', '.h', '.ui', '.qml', '.lua', '.json', '.html', '.qrc', '.in'}
SCANNED = {'.cpp', '.hpp', '.h', '.lua'}
CJK = re.compile('[\u4e00-\u9fff]')

# Comments and character literals are tokens too: quotes inside them must never
# become strings. Adjacent C++ strings may have comments between them.
CPP_TOKENS = re.compile(
    r'(?P<comment>//[^\r\n]*|/\*[\s\S]*?\*/)'
    r'|(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\((?P<rawText>[\s\S]*?)\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*")'
    r"|(?P<character>(?:u8|u|U|L)?'(?:\\[\s\S]|[^'\\])*')")
LUA_TOKENS = re.compile(
    r'(?P<comment>--\[(?P<level>=*)\[[\s\S]*?\](?P=level)\]|--[^\r\n]*)'
    r'|(?P<luaRaw>\[(?P<rawLevel>=*)\[(?P<luaText>[\s\S]*?)\](?P=rawLevel)\])'
    r'|(?P<string>"(?:\\[\s\S]|[^"\\])*"' r"|'(?:\\[\s\S]|[^'\\])*')")
ESCAPE = re.compile(r'\\(?:U[0-9a-fA-F]{8}|u[0-9a-fA-F]{4}|x[0-9a-fA-F]+|[0-7]{1,3}|.)')
SIMPLE_ESCAPES = {'n': '\n', 'r': '\r', 't': '\t', 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v'}
COMMENTS = re.compile(r'//[^\r\n]*|/\*[\s\S]*?\*/')
MARKED = re.compile(r'(?:translate|QT_TRANSLATE_(?:N_)?NOOP)\(\s*"[^"\r\n]+"\s*,\s*$', re.I)
SOURCE_ARGUMENT = re.compile(r'(?:translate|QT_TRANSLATE_(?:N_)?NOOP)\(\s*$', re.I)
COMMENT_ARGUMENT = re.compile(
    r'(?:translate|QT_TRANSLATE_NOOP3)\(\s*"(?:\\.|[^"\\])*"\s*,\s*"(?:\\.|[^"\\])*"\s*,\s*$', re.I)
UI_SETTER = re.compile(
    r'(?:new\s+Q(?:Label|PushButton|CheckBox)|setText|setTitle|setWindowTitle|setToolTip|setPlaceholderText'
    r'|setAccessibleName|addAction|addItem)\(\s*(?:QStringLiteral\(\s*)?$', re.I)
PLACEHOLDER = re.compile(r'%(?:L?(?:[1-9][0-9]?|n))')
TAG = re.compile(r'</?[A-Za-z][A-Za-z0-9]*(?:\s[^<>]*?)?/?>')


def read_source(path):
    # Keeps line endings so literal text matches the exemption list exactly.
    with open(path, encoding='utf-8-sig', newline='') as file:
        return file.read()


def decode_literal(text):
    def replace(match):
        escaped = match.group(0)[1:]
        if escaped in SIMPLE_ESCAPES:
            return SIMPLE_ESCAPES[escaped]
        if escaped[0] in 'uUx' and len(escaped) > 1:
            return chr(int(escaped[1:], 16))
        if re.fullmatch('[0-7]+', escaped):
            return chr(int(escaped, 8))
        return escaped
    return ESCAPE.sub(replace, text)


def literals(source, lua):
    items = []
    last_end = -1
    for match in (LUA_TOKENS if lua else CPP_TOKENS).finditer(source):
        if match.group('comment') is not None:
            continue
        if not lua and match.group('character') is not None:
            last_end = -1
            continue
        raw = not lua and match.group('raw') is not None
        if raw:
            value = match.group('rawText')
        elif lua and match.group('luaRaw') is not None:
            value = match.group('luaText')
        else:
            quoted = match.group('string')
            quote_at = 0 if lua else quoted.index('"')
            value = decode_literal(quoted[quote_at + 1:-1])
        gap = COMMENTS.sub('', source[last_end:match.start()]) if last_end >= 0 else '-'
        if not lua and items and not gap.strip():
            items[-1]['text'] += value
            items[-1]['end'] = match.end()
        else:
            items.append({'text': value, 'start': match.start(), 'end': match.end(), 'raw': raw,
                          'delimiter': match.group('delimiter') if raw else ''})
        last_end = match.end()
    return items


def is_candidate(value, prefix):
    if SOURCE_ARGUMENT.search(prefix) or COMMENT_ARGUMENT.search(prefix):
        return False
    if CJK.search(value) or re.search(r'[A-Za-z][^\n]*\s[^\n]*[A-Za-z]', value):
        return True
    if re.match(r'[A-Z][a-z]+[.!?…]?$', value.replace('&', '')):
        return True
    return bool(re.search('[A-Za-z]', value)) and bool(UI_SETTER.search(prefix))


class Checker:
    def __init__(self, repository, exemptions):
        self.repository = repository
        self.exemptions = exemptions
        self.used = {}
        self.errors = []
        self.counts = []
        self.candidates = []

    def scan(self, relative, source, lua=False, line_offset=0):
        for literal in literals(source, lua):
            start = literal['start']
            prefix = source[max(0, start - 200):start]
            if literal['raw'] and literal['delimiter'] == 'lua':
                self.scan(relative, literal['text'], True, line_offset + source.count('\n', 0, start))
                continue
            if MARKED.search(prefix):
                if CJK.search(literal['text']):
                    self.errors.append(f'{relative} contains a non-English translation source: {literal["text"]}')
                continue
            value = '<QSS stylesheet>' if literal['raw'] and literal['delimiter'] == 'qss' else literal['text']
            if not is_candidate(value, prefix):
                continue
            self.candidates.append({'file': relative, 'text': value})
            matches = [entry for entry in self.exemptions if entry['file'] == relative and entry['text'] == value]
            if len(matches) == 1 and matches[0].get('reason', '').strip():
                key = (relative, value)
                self.used[key] = self.used.get(key, 0) + 1
            else:
                line = line_offset + source.count('\n', 0, start) + 1
                summary = ' '.join(value.split())
                if len(summary) > 140:
                    summary = summary[:140] + '…'
                self.errors.append(f'{relative}:{line}: unmarked text: {summary}')


def text_of(element):
    return ''.join(element.itertext()) if element is not None else ''


def message_key(message, context):
    return '\n'.join([context, text_of(message.find('source')), text_of(message.find('comment')),
                      message.get('numerus', '')])


def placeholders(text):
    return '|'.join(sorted(PLACEHOLDER.findall(text)))


def structure(text):
    return f"{text.count(chr(10))}|{text.replace('&&', '').count('&')}|{'|'.join(TAG.findall(text))}"


def resolve_lupdate(arguments, repository):
    if arguments.lupdate:
        return pathlib.Path(arguments.lupdate)
    qt_root = arguments.qt_root or os.environ.get('QT_ROOT')
    local = repository / '.local/sdk.json'
    if not qt_root and local.is_file():
        qt_root = json.loads(local.read_text(encoding='utf-8')).get('qtRoot')
    if qt_root:
        return pathlib.Path(qt_root) / 'bin' / ('lupdate.exe' if os.name == 'nt' else 'lupdate')
    found = shutil.which('lupdate')
    if not found:
        raise SystemExit('lupdate was not found: pass --lupdate or --qt-root, or set QT_ROOT.')
    return pathlib.Path(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--source-root', default=pathlib.Path(__file__).resolve().parent.parent, type=pathlib.Path)
    parser.add_argument('--qt-root', help='Qt kit directory; defaults to QT_ROOT or .local/sdk.json')
    parser.add_argument('--lupdate', help="Qt's lupdate; CTest passes the build's own")
    parser.add_argument('--catalog', choices=['All'] + [group['name'] for group in GROUPS], default='All')
    parser.add_argument('--report', type=pathlib.Path, help='write a JSON report here')
    arguments = parser.parse_args()
    repository = arguments.source_root.resolve()
    lupdate = resolve_lupdate(arguments, repository)
    if not lupdate.is_file():
        raise SystemExit(f'lupdate was not found: {lupdate}')
    exemptions = json.loads(read_source(repository / 'tools/translation-exemptions.json'))
    checker = Checker(repository, exemptions)
    groups = [group for group in GROUPS if arguments.catalog in ('All', group['name'])]

    with tempfile.TemporaryDirectory(prefix='muxora-translations-') as temporary:
        for group in groups:
            sources = set()
            for root in group['roots']:
                directory = repository / root
                if not directory.is_dir():
                    raise SystemExit(f'Missing translation source directory: {root}')
                for path in directory.rglob('*'):
                    relative = path.relative_to(repository).as_posix()
                    if (path.is_file() and path.suffix.lower() in EXTENSIONS
                            and not re.search(r'/(?:tests|translations|benchmarks)/', relative, re.I)):
                        sources.add(path)
            sources = sorted(sources)
            if not sources:
                raise SystemExit(f'No sources found for {group["name"]}')
            for path in sources:
                if path.suffix.lower() in SCANNED:
                    checker.scan(path.relative_to(repository).as_posix(), read_source(path),
                                 path.suffix.lower() == '.lua')

            fresh = pathlib.Path(temporary) / (group['stem'] + '.ts')
            # Qt reads @list files in the local code page. Its JSON project input
            # keeps Unicode source paths intact and avoids command-line length limits.
            project = pathlib.Path(temporary) / (group['stem'] + '.json')
            project.write_text(json.dumps([{'projectFile': str(repository / 'CMakeLists.txt'),
                                            'sources': [str(path) for path in sources]}], ensure_ascii=False),
                               encoding='utf-8')
            extraction = subprocess.run(
                [str(lupdate), '-project', str(project), '-no-obsolete', '-locations', 'none',
                 '-source-language', 'en', '-target-language', 'en', '-ts', str(fresh)],
                capture_output=True, text=True, errors='replace')
            if extraction.returncode != 0:
                raise SystemExit(f'lupdate failed for {group["name"]}: {extraction.stdout}{extraction.stderr}')
            expected = set()
            for context in ElementTree.parse(fresh).getroot().iter('context'):
                for message in context.iter('message'):
                    expected.add(message_key(message, context.findtext('name', '')))

            for language in ('en', 'zh_CN'):
                path = f'{group["directory"]}/{group["stem"]}_{language}.ts'
                document = ElementTree.fromstring(read_source(repository / path))
                if document.get('sourcelanguage') != 'en' or document.get('language') != language:
                    checker.errors.append(f'{path}: incorrect source or target language')
                actual = set()
                for context in document.iter('context'):
                    for message in context.iter('message'):
                        key = message_key(message, context.findtext('name', ''))
                        source = text_of(message.find('source'))
                        if key in actual:
                            checker.errors.append(f'{path}: duplicate message: {source}')
                        actual.add(key)
                        if CJK.search(source):
                            checker.errors.append(f'{path}: non-English source: {source}')
                        translation = message.find('translation')
                        if translation is None or translation.get('type'):
                            checker.errors.append(f'{path}: missing or unfinished translation: {source}')
                            continue
                        plural = message.get('numerus') == 'yes'
                        forms = translation.findall('numerusform') if plural else [translation]
                        if len(forms) != (2 if plural and language == 'en' else 1):
                            checker.errors.append(f'{path}: incorrect plural form count: {source}')
                        for form in forms:
                            text = text_of(form)
                            if not text.strip():
                                checker.errors.append(f'{path}: empty translation: {source}')
                            if placeholders(text) != placeholders(source):
                                checker.errors.append(f'{path}: placeholders differ: {source}')
                            if structure(text) != structure(source):
                                checker.errors.append(f'{path}: line breaks, mnemonics or markup differ: {source}')
                for key in sorted(expected - actual):
                    checker.errors.append(f'{path}: missing extracted message: {key.replace(chr(10), " | ")}')
                for key in sorted(actual - expected):
                    checker.errors.append(f'{path}: message no longer extracted: {key.replace(chr(10), " | ")}')
                # lupdate owns the file's layout. A catalog edited by hand, or one
                # whose build target was not run, changes when lupdate rewrites it.
                rewritten = pathlib.Path(temporary) / f'{group["stem"]}_{language}.ts'
                shutil.copyfile(repository / path, rewritten)
                update = subprocess.run(
                    [str(lupdate), '-project', str(project), '-no-obsolete', '-locations', 'none',
                     '-source-language', 'en', '-ts', str(rewritten)],
                    capture_output=True, text=True, errors='replace')
                if update.returncode != 0:
                    raise SystemExit(f'lupdate failed for {path}: {update.stdout}{update.stderr}')
                if rewritten.read_bytes() != (repository / path).read_bytes():
                    checker.errors.append(f'{path}: differs from what lupdate writes; build the {group["target"]} '
                                          'target and edit only the translations')
                checker.counts.append({'catalog': path, 'sourceFiles': len(sources),
                                       'extracted': len(expected), 'messages': len(actual)})

    for exemption in exemptions:
        if not any(exemption['file'].startswith(root + '/') for group in groups for root in group['roots']):
            continue
        used = checker.used.get((exemption['file'], exemption['text']), 0)
        if used != exemption['count']:
            checker.errors.append(
                f'{exemption["file"]}: exemption count changed ({used}/{exemption["count"]}): {exemption["text"]}')

    if arguments.report:
        arguments.report.write_text(json.dumps(
            {'passed': not checker.errors, 'catalogs': checker.counts, 'candidates': checker.candidates,
             'errors': checker.errors}, ensure_ascii=False, indent=2), encoding='utf-8')
    if checker.errors:
        for error in checker.errors:
            print(error)
        print(f'Translation check failed with {len(checker.errors)} issue(s).')
        return 1
    print(f'Translation check passed: {len(checker.counts)} catalogs; source extraction, translations and candidate scan.')
    return 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.exit(main())
