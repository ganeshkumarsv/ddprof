# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Sets the relevant parameters for version control
set(BUILD_REV "custom" CACHE STRING "Revision appended to the version information")
list(APPEND DDPROF_DEFINITION_LIST "VER_REV=\"${BUILD_REV}\"")

list(APPEND DDPROF_DEFINITION_LIST "VER_MAJ=${PROJECT_VERSION_MAJOR}")
list(APPEND DDPROF_DEFINITION_LIST "VER_MIN=${PROJECT_VERSION_MINOR}")
list(APPEND DDPROF_DEFINITION_LIST "VER_PATCH=${PROJECT_VERSION_PATCH}")
list(APPEND DDPROF_DEFINITION_LIST "VER_TWEAK=${PROJECT_VERSION_TWEAK}")
