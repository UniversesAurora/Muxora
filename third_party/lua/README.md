# Embedded Lua build

This target downloads the fixed official Lua 5.5.1 source archive into the CMake
build directory and compiles it as C++. The Lua license is included in
`../licenses/lua.txt`. Neither a system Lua installation nor a Lua DLL is needed.

The embedded build has one documented adaptation in `lgc.c`: it keeps the
instruction hook enabled while running `__gc` instead of temporarily disabling
it. Upstream normally disables hooks there. Without this change an accidental
infinite finalizer can block both a stop request and `lua_close`, as reproduced
by `cleanupCannotExecuteUnboundedFinalizers`.

The source archive stays unchanged. CMake creates a build-local copy of that one
file and fails configuration if the exact pinned adaptation site disappears.
Lua upgrades must review the site and rerun the stop-request, finalizer, OOM and
shutdown regressions. No test expectations or time limits are relaxed for this
adaptation. Scripts have the full standard library, including `debug`; user hooks
set with `debug.sethook` run alongside the runtime's own stop check (ADR 0008).
