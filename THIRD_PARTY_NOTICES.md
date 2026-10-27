# Third-party components

Muxora's own source is licensed under MIT. The Windows preview dynamically links
Qt Core, Gui and Widgets. Its bundled TUIO touch plugin also loads Qt Network.
The deployment includes the Microsoft Visual C++ runtime, Mesa's software OpenGL
renderer and Microsoft's HLSL compiler. Lua is compiled into the application.
Each component keeps its own license.

The opt-in, in-progress `WindowSystemQuick` rendering target also links Qt Quick
and its Qt Qml dependencies under LGPLv3. It uses public C++ interfaces and does
not link Qt Canvas Painter. This target is not yet linked into or deployed with
the default preview. See the [Qt Quick licensing documentation](https://doc.qt.io/qt-6/qtquick-index.html),
the [matching qtdeclarative source](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtdeclarative-everywhere-src-6.12.0.zip)
and its [license collection](https://code.qt.io/cgit/qt/qtdeclarative.git/tree/LICENSES?h=v6.12.0).
Actual deployment of that target must retain these runtime notices and licenses.

| Component | Version used | Source and license information |
| --- | --- | --- |
| Qt | 6.12.0 | Core, Gui, Widgets, Network and the deployed Qt plugins are used under LGPLv3. [Matching qtbase source ZIP](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qtbase-everywhere-src-6.12.0.zip), [official v6.12.0 tag](https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.12.0), [Qt licensing](https://doc.qt.io/qt-6/licensing.html). |
| Qt translations (`qtbase_zh_CN.qm`) | 6.12.0 | The unmodified official Simplified Chinese catalog is embedded for standard Qt controls and dialogs under BSD-3-Clause, as identified by the installed Qt translations component manifest. Copyright (C) The Qt Company Ltd. and other contributors. [Matching qttranslations source ZIP](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/qttranslations-everywhere-src-6.12.0.zip), [official v6.12.0 tag](https://code.qt.io/cgit/qt/qttranslations.git/tag/?h=v6.12.0). The attribution and original license text are retained in `licenses/open-source/qt-translations.txt`. |
| Lua | 5.5.1 | Official [source archive](https://www.lua.org/ftp/lua-5.5.1.tar.gz), under the [MIT license](https://www.lua.org/license.html). Copyright (C) 1994–2026 Lua.org, PUC-Rio. The original notice is retained in [`third_party/licenses/lua.txt`](third_party/licenses/lua.txt). The embedded build adaptation is described below. |
| Microsoft Visual C++ runtime (`msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`, `vccorlib140.dll`) | 14.44 (Visual Studio 2022) | Unmodified files from Visual Studio's `VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT` directory, deployed beside the application. Distributable under the Visual Studio license terms for Distributable Code ([Visual Studio 2022 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)). Microsoft terms apply separately. |
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
a stop request also reaches code running during cleanup. CMake generates a build-local copy of
`lgc.c` for this change and checks the expected source location; the downloaded
archive is unchanged. See the [build definition](third_party/lua/CMakeLists.txt)
and [adaptation notes](third_party/lua/README.md). No separate Lua installation or runtime DLL is
required. These notices describe the current source; previously published
preview packages retain the dependency list of their own release.
