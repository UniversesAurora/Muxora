# Third-party components

Muxora's own source is licensed under MIT. The window preview dynamically links
Qt Core, Gui and Widgets; its Windows deployment also includes the matching
MinGW runtime. Their licenses apply independently to those files.

| Component | Tested version | Source and license information |
| --- | --- | --- |
| Qt | 6.12.0 | [Qt source downloads](https://download.qt.io/official_releases/qt/), [Qt licensing](https://doc.qt.io/qt-6/licensing.html). The selected Core/Gui/Widgets modules are used under LGPLv3. |
| MinGW-w64 / GCC runtime | GCC 15.1.0, runtime v12, POSIX/SEH/MSVCRT | [Matching original toolchain release](https://github.com/niXman/mingw-builds-binaries/releases/tag/15.1.0-rt_v12-rev0). Runtime notices and the GCC Runtime Library Exception are copied from the toolchain's `licenses` directory. |

The local deployment contains license texts and Qt's supplied qtbase software
bill of materials. That inventory also identifies libraries bundled into Qt.
Qt runtime DLLs remain separate and replaceable; Muxora does not embed a modified
Qt copy. The SDK and compilers themselves are not distributed with the preview.

This is the first local Windows preview. A public release must retain the actual
deployed component inventory, notices and the corresponding source availability
required by each dependency. The design's remaining release checks are tracked
in `docs/development/testing-design.md`.
