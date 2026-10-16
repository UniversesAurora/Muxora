# Embedded Lua build

This target downloads the fixed official Lua 5.5.1 source archive into the CMake
build directory and compiles it as C++. The Lua license is included in
`../licenses/lua.txt`. Neither a system Lua installation nor a Lua DLL is needed.

The embedded build has one documented adaptation in `lgc.c`: it keeps the
instruction hook enabled while running `__gc` instead of temporarily disabling
it. Upstream normally disables hooks there. Without this change an accidental
infinite finalizer can block both task cancellation and `lua_close`, as reproduced
by `cleanupCannotExecuteUnboundedFinalizers`.

The source archive stays unchanged. CMake creates a build-local copy of that one
file and fails configuration if the exact pinned adaptation site disappears.
Lua upgrades must review the site and rerun the instruction, finalizer, OOM and
shutdown regressions. No test expectations or time limits are relaxed for this
adaptation. This build does not expose the debug library to scripts.
