# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# DAS - Technically, all of this is already specified in the CMakeLists.txt
# provided in the LLVM demangling source dir, but I can't be bothered to mock
# their add_llvm_component cmake definition

# Defines :
# target : llvm-demangle
# variable :
#    LLVM_DEMANGLE_SOURCES
#    LLVM_DEMANGLE_PATH

set(LLVM_DEMANGLE_PATH ${CMAKE_SOURCE_DIR}/vendor/llvm CACHE STRING " Path to the llvm-demangle directory")
set(LLVM_DEMANGLE_SVNROOT https://github.com/llvm/llvm-project/trunk/llvm CACHE STRING " GitHub URL for LLVM")
set(LLVM_DEMANGLE_SRC ${CMAKE_SOURCE_DIR}/vendor/llvm/lib/Demangle CACHE STRING "Path to the llvm-demangle sources")

set(LLVM_DEMANGLE_SRC_FILES
    ${LLVM_DEMANGLE_SRC}/Demangle.cpp
    ${LLVM_DEMANGLE_SRC}/ItaniumDemangle.cpp
    ${LLVM_DEMANGLE_SRC}/MicrosoftDemangle.cpp
    ${LLVM_DEMANGLE_SRC}/MicrosoftDemangleNodes.cpp
    ${LLVM_DEMANGLE_SRC}/RustDemangle.cpp)

add_custom_command(OUTPUT ${LLVM_DEMANGLE_SRC_FILES}
                   COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_llvm_demangler.sh"
                   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                   COMMENT "Fetching llvm dependencies")

add_custom_target(llvm-deps DEPENDS ${LLVM_DEMANGLE_SRC_FILES})

add_library(llvm-demangle STATIC
            ${LLVM_DEMANGLE_SRC_FILES})
set_property(TARGET llvm-demangle PROPERTY CXX_STANDARD 14)

add_dependencies(llvm-demangle llvm-deps)

target_include_directories(llvm-demangle PUBLIC
  ${LLVM_DEMANGLE_PATH}/include
)
aux_source_directory(${LLVM_DEMANGLE_PATH}/lib/Demangle LLVM_DEMANGLE_SOURCES)
