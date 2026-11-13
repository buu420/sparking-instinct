cmake_minimum_required(VERSION 3.23)
project(SparkingZeroAccessReloaded VERSION 0.1.1 LANGUAGES C CXX)

if(NOT MSVC OR NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build SparkingZeroAccess with MSVC for Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/utf-8)

include(CTest)
include(FetchContent)
FetchContent_Declare(sparking_lua_source
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9FBF5E28EF86C69858F6D3D34ECCC32E911C1A28B4120FF3E84AAA70CFBF1E30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sparking_lua_source)
file(GLOB lua_sources CONFIGURE_DEPENDS "${sparking_lua_source_SOURCE_DIR}/src/*.c")
list(FILTER lua_sources EXCLUDE REGEX "/lua(c)?\\.c$")
add_library(sparking_lua STATIC ${lua_sources})
target_include_directories(sparking_lua PUBLIC "${sparking_lua_source_SOURCE_DIR}/src")
target_compile_definitions(sparking_lua PRIVATE _CRT_SECURE_NO_WARNINGS)

FetchContent_Declare(minhook
    URL https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4
    URL_HASH SHA256=172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(minhook)

set(SPARKING_PRISM_LUA_TARGET sparking_lua)
add_subdirectory(speech)

add_library(sparking_engine STATIC native/unreal.cpp native/lua_unreal.cpp)
target_include_directories(sparking_engine PUBLIC native)
target_link_libraries(sparking_engine PRIVATE sparking_lua)
target_compile_options(sparking_engine PRIVATE /W4)

add_library(sparking_runtime STATIC native/runtime.cpp lua/owned_files.cpp)
target_include_directories(sparking_runtime PUBLIC native)
target_link_libraries(sparking_runtime PRIVATE sparking_engine sparking_lua sparking_prism user32)
target_compile_options(sparking_runtime PRIVATE /W4)

add_library(sparking_executable STATIC native/executable.cpp)
target_link_libraries(sparking_executable PRIVATE bcrypt)
target_compile_options(sparking_executable PRIVATE /W4)

add_library(SparkingZeroAccess.Native SHARED native/entry.cpp native/profile.cpp)
target_link_libraries(SparkingZeroAccess.Native PRIVATE sparking_runtime sparking_engine
    sparking_executable sparking_prism minhook user32)
target_link_options(SparkingZeroAccess.Native PRIVATE /PDBALTPATH:SparkingZeroAccess.Native.pdb)
target_compile_options(SparkingZeroAccess.Native PRIVATE /W4)

set(SPARKING_NETHOST_ROOT "" CACHE PATH "Microsoft.NETCore.App.Host.win-x64 9.0.15 package root")
if(SPARKING_NETHOST_ROOT)
    set(nethost_native "${SPARKING_NETHOST_ROOT}/runtimes/win-x64/native")
    file(SHA256 "${nethost_native}/libnethost.lib" nethost_hash)
    if(NOT nethost_hash STREQUAL "fc6453c4c2ae48ac0752a14f6661a074d8bef203f728a481436ec8553ad1ad43")
        message(FATAL_ERROR "Use the pinned Microsoft.NETCore.App.Host.win-x64 9.0.15 package.")
    endif()
    add_library(SparkingZeroAccess.AutoStart SHARED autostart/dsound.cpp autostart/host.cpp autostart/dsound.def)
    target_include_directories(SparkingZeroAccess.AutoStart PRIVATE "${nethost_native}")
    target_link_libraries(SparkingZeroAccess.AutoStart PRIVATE sparking_executable "${nethost_native}/libnethost.lib" advapi32)
    target_link_options(SparkingZeroAccess.AutoStart PRIVATE /PDBALTPATH:SparkingZeroAccess.AutoStart.pdb)
    target_compile_options(SparkingZeroAccess.AutoStart PRIVATE /W4)
    set_target_properties(SparkingZeroAccess.AutoStart PROPERTIES OUTPUT_NAME dsound)
endif()

if(BUILD_TESTING)
    add_executable(sparking_dsound_forward_test autostart/tests/dsound_forward_test.cpp)
    target_link_libraries(sparking_dsound_forward_test PRIVATE dsound)
    add_executable(sparking_dsound_dynamic_test autostart/tests/dsound_forward_test.cpp)
    target_compile_definitions(sparking_dsound_dynamic_test PRIVATE SPARKING_DSOUND_DYNAMIC_FIXTURE)

    add_executable(sparking_unreal_test native/tests/unreal_test.cpp)
    target_link_libraries(sparking_unreal_test PRIVATE sparking_engine sparking_lua)
    add_test(NAME sparking_unreal COMMAND sparking_unreal_test)

    add_executable(sparking_owned_files_test lua/tests/owned_files_test.cpp lua/owned_files.cpp)
    target_include_directories(sparking_owned_files_test PRIVATE lua)
    target_link_libraries(sparking_owned_files_test PRIVATE sparking_lua)
    add_test(NAME sparking_owned_files COMMAND sparking_owned_files_test)
endif()

