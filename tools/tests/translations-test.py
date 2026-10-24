#!/usr/bin/env python3
"""Reverse cases for tools/check-translations.py.

Copies the sources into a temporary directory whose name has non-ASCII
characters and a space, breaks one thing at a time and checks that the checker
rejects each broken copy for the stated reason. The working tree is never
changed. CTest runs it as translations.checker.

Usage: python tools/tests/translations-test.py [--lupdate PATH | --qt-root DIR]
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent.parent
CATALOG = 'modules/window-system/translations/window_system_zh_CN.ts'


class Fixture:
    def __init__(self, root, checker_arguments):
        self.root = root
        self.checker_arguments = checker_arguments
        self.originals = {}
        self.results = []

    def change_source(self, relative, change):
        path = self.root / relative
        original = path.read_text(encoding='utf-8')
        self.originals[path] = path.read_bytes()
        replacement = change(original)
        if replacement == original:
            raise SystemExit(f'Mutation did not change {relative}')
        path.write_bytes(replacement.encode('utf-8'))

    def change_catalog(self, source, change):
        path = self.root / CATALOG
        self.originals[path] = path.read_bytes()
        tree = ElementTree.parse(path)
        found = [(context, message) for context in tree.getroot().iter('context')
                 for message in context.iter('message') if message.findtext('source') == source]
        if len(found) != 1:
            raise SystemExit(f'Expected one catalog entry: {source}')
        change(*found[0])
        tree.write(path, encoding='utf-8', xml_declaration=True)

    def check(self, name, mutation=None, expected_error=''):
        try:
            if mutation:
                mutation()
            report = self.root / 'result.json'
            report.unlink(missing_ok=True)
            run = subprocess.run([sys.executable, str(REPOSITORY / 'tools/check-translations.py'),
                                  '--source-root', str(self.root), '--report', str(report), *self.checker_arguments],
                                 capture_output=True, text=True, encoding='utf-8', errors='replace')
            output = run.stdout + run.stderr
            if not report.is_file():
                raise SystemExit(f'{name}: checker did not produce a report: {output}')
            result = json.loads(report.read_text(encoding='utf-8'))
            if expected_error:
                if run.returncode == 0 or result['passed'] or not any(expected_error in error for error in result['errors']):
                    raise SystemExit(f"{name}: expected rejection containing '{expected_error}', got {output}")
            elif run.returncode != 0 or not result['passed']:
                raise SystemExit(f'{name}: valid fixture failed: {output}')
            self.results.append({'name': name, 'passed': True, 'checkerExitCode': run.returncode,
                                 'expectedError': expected_error, 'errors': result['errors']})
            print(f'PASS: {name}')
        finally:
            for path, original in self.originals.items():
                path.write_bytes(original)
            self.originals.clear()


def set_translation(text):
    def change(context, message):
        translation = message.find('translation')
        for child in list(translation):
            translation.remove(child)
        translation.text = text
    return change


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--qt-root')
    parser.add_argument('--lupdate')
    parser.add_argument('--report', type=pathlib.Path)
    arguments = parser.parse_args()
    checker_arguments = []
    if arguments.lupdate:
        checker_arguments += ['--lupdate', arguments.lupdate]
    if arguments.qt_root:
        checker_arguments += ['--qt-root', arguments.qt_root]
    if not arguments.lupdate and not arguments.qt_root:
        # The copy has no .local/sdk.json, so resolve lupdate from the real tree.
        local = REPOSITORY / '.local/sdk.json'
        if local.is_file():
            checker_arguments += ['--qt-root', json.loads(local.read_text(encoding='utf-8'))['qtRoot']]

    with tempfile.TemporaryDirectory(prefix='muxora-translation-tests-检查 ') as temporary:
        root = pathlib.Path(temporary)
        for directory in ('modules', 'src'):
            shutil.copytree(REPOSITORY / directory, root / directory)
        (root / 'tools').mkdir()
        shutil.copy2(REPOSITORY / 'tools/translation-exemptions.json', root / 'tools')
        fixture = Fixture(root, checker_arguments)
        manager = 'modules/window-system/src/window_manager.cpp'

        fixture.check('valid source and catalogs')
        fixture.check('unmarked UI literal', lambda: fixture.change_source(
            manager, lambda text: text + '\nvoid translationFixture() { new QLabel("Untranslated test label"); }\n'),
            'unmarked text: Untranslated test label')
        fixture.check('unmarked single-word UI literal', lambda: fixture.change_source(
            manager, lambda text: text + '\nvoid translationFixture() { new QLabel("settings" /* label */); }\n'),
            'unmarked text: settings')
        fixture.check('removed static-table marker', lambda: fixture.change_source(
            'modules/window-system/src/commands/tab_group_commands.cpp',
            lambda text: text.replace('QT_TRANSLATE_NOOP("WindowSystem", "Name: A to Z")', '"Name: A to Z"')),
            'message no longer extracted: WindowSystem | Name: A to Z')
        fixture.check('missing catalog message', lambda: fixture.change_catalog(
            'Settings', lambda context, message: context.remove(message)),
            'missing extracted message: WindowSystem | Settings')
        fixture.check('damaged placeholder', lambda: fixture.change_catalog(
            'Command was not executed: %1', set_translation('命令未执行')),
            'placeholders differ: Command was not executed: %1')
        fixture.check('unfinished translation', lambda: fixture.change_catalog(
            'Settings', lambda context, message: message.find('translation').set('type', 'unfinished')),
            'missing or unfinished translation: Settings')
        fixture.check('empty translation', lambda: fixture.change_catalog('Settings', set_translation('')),
                      'empty translation: Settings')
        fixture.check('missing plural form', lambda: fixture.change_catalog(
            '%n tabs', lambda context, message: message.find('translation').clear()),
            'incorrect plural form count: %n tabs')
        fixture.check('damaged mnemonic', lambda: fixture.change_catalog('&File', set_translation('文件')),
                      'mnemonics or markup differ: &File')
        fixture.check('catalog formatted by hand', lambda: fixture.change_catalog(
            'Settings', lambda context, message: None),
            'differs from what lupdate writes')
        fixture.check('valid fixture after every mutation is undone')

    if arguments.report:
        arguments.report.write_text(json.dumps({'passed': True, 'cases': fixture.results},
                                               ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'Translation checker regression passed: {len(fixture.results)} cases.')
    return 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.exit(main())
