cmake_minimum_required(VERSION 3.21)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(BUILD_PRESETS hoja pico-w pico-2w)

foreach(preset IN LISTS BUILD_PRESETS)
    message(STATUS "Configuring ${preset}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --preset "${preset}"
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        RESULT_VARIABLE configure_result
    )
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR
            "Configuration failed for preset '${preset}' (${configure_result})")
    endif()

    message(STATUS "Building ${preset}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build --preset "${preset}"
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        RESULT_VARIABLE build_result
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR
            "Build failed for preset '${preset}' (${build_result})")
    endif()
endforeach()

message(STATUS "All firmware variants built successfully")
