include(FetchContent)

# The upstream archive and its published digest are fixed together. Sources
# stay in the build tree; consumers do not need a separately installed SDK.
FetchContent_Declare(muxora_lua_source
    URL https://www.lua.org/ftp/lua-5.5.1.tar.gz
    URL_HASH SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(muxora_lua_source)

set(muxora_lua_source_names
    lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump
    lfunc lgc linit liolib llex lmathlib lmem loadlib lobject lopcodes
    loslib lparser lstate lstring lstrlib ltable ltablib ltm lundump
    lutf8lib lvm lzio)
set(muxora_lua_sources)
foreach(name IN LISTS muxora_lua_source_names)
    list(APPEND muxora_lua_sources "${muxora_lua_source_SOURCE_DIR}/src/${name}.c")
endforeach()

# Upstream suppresses debug hooks while executing __gc. An embedded task hook
# would consequently be unable to stop an accidental infinite finalizer during
# allocation or lua_close(). Keep hooks enabled in this pinned build; ordinary
# standalone Lua is not modified. Fail explicitly if upstream changes the site.
file(READ "${muxora_lua_source_SOURCE_DIR}/src/lgc.c" muxora_lua_gc)
set(muxora_gc_original "L->allowhook = 0;  /* stop debug hooks during GC metamethod */")
string(FIND "${muxora_lua_gc}" "${muxora_gc_original}" muxora_gc_location)
if(muxora_gc_location LESS 0)
    message(FATAL_ERROR "Lua GC hook adaptation requires review for this source version")
endif()
string(REPLACE "${muxora_gc_original}"
    "/* Muxora embedding: retain the task instruction hook during __gc. */"
    muxora_lua_gc "${muxora_lua_gc}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lgc-budgeted.cpp" CONTENT "${muxora_lua_gc}" @ONLY)
list(REMOVE_ITEM muxora_lua_sources "${muxora_lua_source_SOURCE_DIR}/src/lgc.c")
list(APPEND muxora_lua_sources "${CMAKE_CURRENT_BINARY_DIR}/lgc-budgeted.cpp")

# C++ exceptions unwind C++ binding frames correctly when an allocation or
# type check raises a Lua error. Do not mix this target with a C-mode Lua DLL.
set_source_files_properties(${muxora_lua_sources} PROPERTIES LANGUAGE CXX)
add_library(MuxoraLua STATIC ${muxora_lua_sources})
add_library(Muxora::Lua ALIAS MuxoraLua)
target_compile_features(MuxoraLua PRIVATE cxx_std_20)
target_include_directories(MuxoraLua SYSTEM PUBLIC "${muxora_lua_source_SOURCE_DIR}/src")
set_target_properties(MuxoraLua PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX)
    target_link_libraries(MuxoraLua PRIVATE ${CMAKE_DL_LIBS} m)
endif()
