# Third-party components

Muxora's own source is licensed under MIT. The Windows preview dynamically links
Qt Core, Gui and Widgets. Its bundled TUIO touch plugin also loads Qt Network.
The deployment includes the matching MinGW runtime, Mesa's software OpenGL
renderer and Microsoft's HLSL compiler. Lua is compiled into the application.
Each component keeps its own license.

| Component | Version used | Source and license information |
| --- | --- | --- |
| Qt | 6.12.0 | Core, Gui, Widgets, Network and the deployed Qt plugins are used under LGPLv3. [Matching qtbase source ZIP](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtbase-everywhere-src-6.12.0.zip), [official v6.12.0 tag](https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.12.0), [Qt licensing](https://doc.qt.io/qt-6/licensing.html). |
| Qt translations (`qtbase_zh_CN.qm`) | 6.12.0 | The unmodified official Simplified Chinese catalog is embedded for standard Qt controls and dialogs under BSD-3-Clause, as identified by the installed Qt translations component manifest. Copyright (C) The Qt Company Ltd. and other contributors. [Matching qttranslations source ZIP](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qttranslations-everywhere-src-6.12.0.zip), [official v6.12.0 tag](https://code.qt.io/cgit/qt/qttranslations.git/tag/?h=v6.12.0). The attribution and original license text are retained in `licenses/open-source/qt-translations.txt`. |
| Lua | 5.5.1 | Official [source archive](https://www.lua.org/ftp/lua-5.5.1.tar.gz), under the [MIT license](https://www.lua.org/license.html). Copyright (C) 1994–2026 Lua.org, PUC-Rio. The original notice is retained in [`third_party/licenses/lua.txt`](third_party/licenses/lua.txt). The embedded build adaptation is described below. |
| MinGW-w64 / GCC runtime | GCC 15.1.0, runtime v12, POSIX/SEH/MSVCRT | [Matching original toolchain release](https://github.com/niXman/mingw-builds-binaries/releases/tag/15.1.0-rt_v12-rev0). Runtime notices and the GCC Runtime Library Exception are copied from the toolchain's `licenses` directory. |
| Mesa llvmpipe (`opengl32sw.dll`) | 11.2.2 | Unmodified renderer supplied with the Qt Windows kit (`opengl32sw-64-mesa_11_2_2-signed_sha256`). [Qt's Mesa attribution](https://doc.qt.io/qt-6/qt-attribution-llvmpipe.html) lists MIT and Boost Software License 1.0 terms; their notices are retained in `licenses/open-source/mesa-llvmpipe.txt`. |
| Microsoft Direct3D HLSL Compiler (`D3Dcompiler_47.dll`) | 6.3.9600.16384 | Unmodified Microsoft redistributable supplied with the Qt Windows kit. Copyright Microsoft Corporation. [Microsoft's DirectX SDK redistribution guidance](https://learn.microsoft.com/en-us/windows/win32/directx-sdk--august-2009-) describes distributing this DLL beside an application. Microsoft terms apply separately. |

The deployed Qt kit's `qtbase-sbom.spdx.json` identifies commit
`025bdad181de241e81bf853c8a2d7bf3d19261a9`, which is the official `v6.12.0` tag.
The matching source ZIP has SHA-256
`0529901575c413e34ded504eeda177c4a6dfd0dff1e39ed52c811dc5204b43c7`, as listed
on [Qt's download details page](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtbase-everywhere-src-6.12.0.zip.mirrorlist).

The deployment retains the source archive's top-level `LICENSES/*.txt` collection
unchanged in `licenses/open-source/qtbase-6.12.0/`. Qt's supplied
`licenses/qtbase-sbom.spdx.json` records component copyright notices and license
identifiers to read with those texts. Qt runtime DLLs remain separate and
replaceable. Muxora does not change the Qt source. The SDK and compiler tools
themselves are not distributed with the preview.

Lua is built from the fixed upstream archive as C++ and statically linked. The
embedded build keeps the instruction hook enabled during `__gc` finalizers so
task budgets also apply during cleanup. CMake generates a build-local copy of
`lgc.c` for this change and checks the expected source location; the downloaded
archive is unchanged. See the [build definition](third_party/lua/CMakeLists.txt)
and [adaptation notes](third_party/lua/README.md). This does not expose Lua's
debug library to user scripts. No separate Lua installation or runtime DLL is
required. These notices describe the current source; previously published
preview packages retain the dependency list of their own release.
