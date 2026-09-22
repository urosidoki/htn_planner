# Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com
cmake_minimum_required(VERSION 3.25)
project(HTNLinuxSmoke LANGUAGES C CXX)
enable_testing()
get_filename_component(HTN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build the generated runtime and translator. GUI tools and Optick capture
# are separate portability gates.
file(GLOB_RECURSE HTN_SOURCES CONFIGURE_DEPENDS "${HTN_ROOT}/HTNFramework/src/*.cpp")
list(FILTER HTN_SOURCES EXCLUDE REGEX "/pch\\.cpp$")
add_library(HTNFramework STATIC ${HTN_SOURCES})
target_include_directories(HTNFramework PUBLIC "${HTN_ROOT}/HTNFramework/src"
    PRIVATE "${HTN_ROOT}/ThirdParty/optick/src")
target_compile_definitions(HTNFramework PUBLIC
    $<$<CONFIG:Debug>:HTN_DEBUG;HTN_ENABLE_LOGGING;HTN_VALIDATE_DOMAIN;HTN_DEBUG_DECOMPOSITION>
    $<$<CONFIG:Release>:HTN_RELEASE>
    PRIVATE USE_OPTICK=0)
target_precompile_headers(HTNFramework PRIVATE "${HTN_ROOT}/HTNFramework/src/pch.h")
find_package(Threads REQUIRED)
target_link_libraries(HTNFramework PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
    # clang-cl treats -Wall as MSVC /Wall; pass the Clang warning groups explicitly.
    target_compile_options(HTNFramework PRIVATE /clang:-Wall /clang:-Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(HTNFramework PRIVATE -Wall -Wextra)
endif()

add_executable(HTNTranslator "${HTN_ROOT}/HTNTranslator/src/main.cpp")
target_link_libraries(HTNTranslator PRIVATE HTNFramework)
add_library(HTNRuntimeBridge SHARED "${HTN_ROOT}/HTNRuntimeBridge/src/HTNRuntimeBridge.cpp")
target_include_directories(HTNRuntimeBridge PRIVATE "${HTN_ROOT}/HTNFramework/src")
target_compile_definitions(HTNRuntimeBridge PRIVATE
    $<$<CONFIG:Debug>:HTN_DEBUG;HTN_ENABLE_LOGGING;HTN_VALIDATE_DOMAIN;HTN_DEBUG_DECOMPOSITION>
    $<$<CONFIG:Release>:HTN_RELEASE>)

set(DOMAIN "${CMAKE_CURRENT_LIST_DIR}/linux_smoke.domain")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/linux_smoke.generated.c")
add_custom_command(OUTPUT "${GENERATED}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND HTNTranslator "${DOMAIN}" CreateLinuxSmokeHTN "${CMAKE_CURRENT_BINARY_DIR}/generated"
        --runtime-backtracking-support=enabled
    DEPENDS HTNTranslator "${DOMAIN}" VERBATIM)
add_executable(HTNLinuxSmoke "${CMAKE_CURRENT_LIST_DIR}/main.cpp" "${GENERATED}")
target_link_libraries(HTNLinuxSmoke PRIVATE HTNFramework)
add_test(NAME generated_execution COMMAND HTNLinuxSmoke)
add_test(NAME translator_check COMMAND HTNTranslator --check "${DOMAIN}")
add_test(NAME translator_includes COMMAND HTNTranslator --check "${HTN_ROOT}/Domains/AAACombatNPC.domain")
