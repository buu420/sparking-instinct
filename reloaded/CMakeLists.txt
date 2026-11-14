cmake_minimum_required(VERSION 3.23)
project(SparkingInstinct VERSION 0.2.0 LANGUAGES C CXX)

if(NOT MSVC OR NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build SparkingZeroAccess with MSVC for Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/utf-8)

include(CTest)
include(FetchContent)
FetchContent_Declare(minhook
    URL https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4
    URL_HASH SHA256=172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(minhook)

add_subdirectory(speech)
include(native/movie_cues.cmake)

add_library(sparking_engine STATIC native/unreal.cpp native/game_reader.cpp)
target_include_directories(sparking_engine PUBLIC native)
target_compile_options(sparking_engine PRIVATE /W4)

add_library(sparking_runtime STATIC native/runtime.cpp native/reading_state.cpp
    native/ui_reader.cpp native/battle_reader.cpp native/movie_reader.cpp native/movie_audio.cpp)
target_include_directories(sparking_runtime PUBLIC native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(sparking_runtime PRIVATE sparking_engine sparking_prism user32 xaudio2 ole32)
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
    target_link_libraries(sparking_unreal_test PRIVATE sparking_engine)
    add_test(NAME sparking_unreal COMMAND sparking_unreal_test)

    add_executable(sparking_reading_state_test native/tests/reading_state_test.cpp native/reading_state.cpp)
    target_include_directories(sparking_reading_state_test PRIVATE native)
    add_test(NAME sparking_reading_state COMMAND sparking_reading_state_test)

    add_executable(sparking_ui_reader_test native/tests/ui_reader_test.cpp)
    target_link_libraries(sparking_ui_reader_test PRIVATE sparking_runtime)
    add_test(NAME sparking_ui_reader COMMAND sparking_ui_reader_test)

    add_executable(sparking_battle_reader_test native/battle_reader.cpp native/tests/battle_reader_test.cpp)
    target_include_directories(sparking_battle_reader_test PRIVATE native)
    add_test(NAME sparking_battle_reader COMMAND sparking_battle_reader_test)


    add_executable(sparking_movie_reader_test native/tests/movie_reader_test.cpp)
    target_link_libraries(sparking_movie_reader_test PRIVATE sparking_runtime)
    add_test(NAME sparking_movie_reader COMMAND sparking_movie_reader_test "${CMAKE_CURRENT_SOURCE_DIR}/narration")
endif()
