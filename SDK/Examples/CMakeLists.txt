cmake_minimum_required(VERSION 3.25)
project(HTNPackageConsumers LANGUAGES C CXX)
find_package(HTN CONFIG REQUIRED PATHS "${HTN_DIR}" NO_DEFAULT_PATH)
enable_testing()
foreach(kind Core Integration)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${kind}")
    add_custom_command(OUTPUT "${generated}/example.generated.c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${generated}"
        COMMAND HTN::HTNTranslator "${CMAKE_CURRENT_SOURCE_DIR}/${kind}Consumer/example.domain"
            "CreatePackage${kind}ConsumerHTN" "${generated}"
        DEPENDS HTN::HTNTranslator "${CMAKE_CURRENT_SOURCE_DIR}/${kind}Consumer/example.domain"
        VERBATIM)
    add_executable(${kind}Consumer "${kind}Consumer/main.cpp" "${generated}/example.generated.c")
    if(kind STREQUAL "Core")
        target_link_libraries(${kind}Consumer PRIVATE HTN::HTNFramework)
    else()
        target_link_libraries(${kind}Consumer PRIVATE HTN::HTNIntegration)
    endif()
    htn_configure_target(${kind}Consumer)
    add_test(NAME ${kind}Consumer COMMAND ${kind}Consumer)
endforeach()

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Bridge/coverage.generated.c"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Bridge"
    COMMAND HTN::HTNTranslator "${CMAKE_CURRENT_SOURCE_DIR}/BridgeConsumer/coverage.domain"
        CreateBridgeCoverageHTN "${CMAKE_CURRENT_BINARY_DIR}/Bridge" --runtime-backtracking-support=enabled
    DEPENDS HTN::HTNTranslator "${CMAKE_CURRENT_SOURCE_DIR}/BridgeConsumer/coverage.domain"
    VERBATIM)
# DomainModule deliberately links ONLY the runtime bridge, never the framework.
add_library(DomainModule SHARED "${CMAKE_CURRENT_BINARY_DIR}/Core/example.generated.c"
    "${CMAKE_CURRENT_BINARY_DIR}/Bridge/coverage.generated.c" BridgeConsumer/exports.cpp)
add_dependencies(DomainModule CoreConsumer)
target_compile_definitions(DomainModule PRIVATE HTN_GENERATED_MODULE_EXPORTS)
target_link_libraries(DomainModule PRIVATE HTN::HTNRuntimeBridge)
htn_configure_target(DomainModule)
add_executable(BridgeConsumer BridgeConsumer/main.cpp)
target_link_libraries(BridgeConsumer PRIVATE HTN::HTNFramework)
htn_configure_target(BridgeConsumer)
add_dependencies(BridgeConsumer DomainModule)
file(READ "${HTN_DIR}/../manifest.json" _validation_manifest)
string(JSON _variant_count LENGTH "${_validation_manifest}" variants)
math(EXPR _variant_last "${_variant_count} - 1")
foreach(_i RANGE ${_variant_last})
    string(JSON _id GET "${_validation_manifest}" variants ${_i} id)
    if(_id STREQUAL HTN_VARIANT_VALIDATION)
        string(JSON _bridge_abi GET "${_validation_manifest}" variants ${_i} runtime_bridge_abi)
        string(JSON _planner_abi GET "${_validation_manifest}" variants ${_i} generated_planner_abi)
        target_compile_definitions(BridgeConsumer PRIVATE HTN_EXPECTED_BRIDGE_ABI=${_bridge_abi}
            HTN_EXPECTED_PLANNER_ABI=${_planner_abi})
    endif()
endforeach()
add_custom_command(TARGET BridgeConsumer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:HTN::HTNRuntimeBridge>" "$<TARGET_FILE_DIR:BridgeConsumer>"
    VERBATIM)
add_test(NAME BridgeConsumer COMMAND BridgeConsumer "$<TARGET_FILE:DomainModule>" "$<TARGET_FILE:HTN::HTNRuntimeBridge>")
get_filename_component(_linker_dir "${CMAKE_LINKER}" DIRECTORY)
add_test(NAME BridgeSymbols COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/ValidateBridgeSymbols.ps1"
    -ObjectRoot "${CMAKE_CURRENT_BINARY_DIR}/DomainModule.dir/$<CONFIG>"
    -Bridge "$<TARGET_FILE:HTN::HTNRuntimeBridge>"
    -ImportLibrary "$<TARGET_LINKER_FILE:HTN::HTNRuntimeBridge>"
    -Dumpbin "${_linker_dir}/dumpbin.exe")

if(HTN_VALIDATE_NEGATIVE_CASES)
    # Validation deliberately links a /MDd client to the /MTd SDK.
    add_executable(RuntimeMismatch EXCLUDE_FROM_ALL CoreConsumer/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/Core/example.generated.c")
    add_dependencies(RuntimeMismatch CoreConsumer)
    target_link_libraries(RuntimeMismatch PRIVATE HTN::HTNFramework)
    htn_configure_target(RuntimeMismatch)
    set_property(TARGET RuntimeMismatch PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
endif()
