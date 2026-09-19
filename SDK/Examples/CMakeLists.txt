cmake_minimum_required(VERSION 3.25)
project(HTNPackageConsumers LANGUAGES C CXX)
find_package(HTN CONFIG REQUIRED)
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

# The same generated C now resides in a module importing the runtime bridge.
add_library(DomainModule SHARED "${CMAKE_CURRENT_BINARY_DIR}/Core/example.generated.c")
add_dependencies(DomainModule CoreConsumer)
target_compile_definitions(DomainModule PRIVATE HTN_GENERATED_MODULE_EXPORTS)
target_link_libraries(DomainModule PRIVATE HTN::HTNRuntimeBridge)
htn_configure_target(DomainModule)
add_executable(BridgeConsumer BridgeConsumer/main.cpp)
target_link_libraries(BridgeConsumer PRIVATE HTN::HTNFramework)
htn_configure_target(BridgeConsumer)
add_dependencies(BridgeConsumer DomainModule)
add_custom_command(TARGET BridgeConsumer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:HTN::HTNRuntimeBridge>" "$<TARGET_FILE_DIR:BridgeConsumer>"
    VERBATIM)
add_test(NAME BridgeConsumer COMMAND BridgeConsumer "$<TARGET_FILE:DomainModule>" "$<TARGET_FILE:HTN::HTNRuntimeBridge>")

if(HTN_VALIDATE_NEGATIVE_CASES)
    # Validation deliberately links a /MDd client to the /MTd SDK.
    add_executable(RuntimeMismatch EXCLUDE_FROM_ALL CoreConsumer/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/Core/example.generated.c")
    add_dependencies(RuntimeMismatch CoreConsumer)
    target_link_libraries(RuntimeMismatch PRIVATE HTN::HTNFramework)
    htn_configure_target(RuntimeMismatch)
    set_property(TARGET RuntimeMismatch PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
endif()
