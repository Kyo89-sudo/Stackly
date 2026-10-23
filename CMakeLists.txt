cmake_minimum_required(VERSION 3.16)
project(Stackly LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- applicazione ---
add_executable(Stackly WIN32 src/main.cpp src/app.rc)
target_compile_definitions(Stackly PRIVATE UNICODE _UNICODE)
target_include_directories(Stackly PRIVATE src)
target_link_libraries(Stackly PRIVATE comctl32 shell32 ole32 winhttp windowscodecs uuid uxtheme dwmapi gdiplus)

# --- installer (incorpora Stackly.exe) ---
set(PAYLOAD_DIR ${CMAKE_SOURCE_DIR}/installer/payload)
add_custom_command(TARGET Stackly POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PAYLOAD_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Stackly> ${PAYLOAD_DIR}/Stackly.exe)
add_executable(StacklySetup WIN32 installer/setup.cpp installer/setup.rc)
add_dependencies(StacklySetup Stackly)
target_compile_definitions(StacklySetup PRIVATE UNICODE _UNICODE)
target_include_directories(StacklySetup PRIVATE installer)
target_link_libraries(StacklySetup PRIVATE comctl32 shell32 ole32 uuid shlwapi msimg32)

foreach(t Stackly StacklySetup)
    if(MSVC)
        target_compile_options(${t} PRIVATE /utf-8 /W4 /permissive-)
        set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(${t} PRIVATE /MANIFEST:NO)
    else()
        target_compile_options(${t} PRIVATE -Wall -municode)
        target_link_options(${t} PRIVATE -municode -static -s)
    endif()
endforeach()
