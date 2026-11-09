if(NOT PB_MODE OR NOT PB_CONSUMER_SOURCE OR NOT PB_CONSUMER_BINARY)
    message(FATAL_ERROR "Consumer test arguments are incomplete")
endif()

file(REMOVE_RECURSE "${PB_CONSUMER_BINARY}")

set(generator_args -G "${PB_GENERATOR}")
if(PB_GENERATOR_PLATFORM)
    list(APPEND generator_args -A "${PB_GENERATOR_PLATFORM}")
endif()

if(PB_MODE STREQUAL "install")
    file(REMOVE_RECURSE "${PB_INSTALL_PREFIX}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${PB_AUDIO_STATS_BINARY}"
                --prefix "${PB_INSTALL_PREFIX}" --config "${PB_CONFIG}"
        RESULT_VARIABLE install_result)
    if(NOT install_result EQUAL 0)
        message(FATAL_ERROR "pbAudioStats install failed: ${install_result}")
    endif()
endif()

set(configure_args
    -S "${PB_CONSUMER_SOURCE}"
    -B "${PB_CONSUMER_BINARY}"
    ${generator_args}
    -DPB_MODE=${PB_MODE}
    -DPB_AUDIO_STATS_SOURCE=${PB_AUDIO_STATS_SOURCE})

if(PB_MODE STREQUAL "install")
    list(APPEND configure_args
        -DCMAKE_PREFIX_PATH=${PB_INSTALL_PREFIX}
        -DPB_CONSUMER_STATIC_RUNTIME=ON)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${configure_args}
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Consumer configure failed: ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${PB_CONSUMER_BINARY}"
            --config "${PB_CONFIG}" --parallel
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Consumer build failed: ${build_result}")
endif()

set(executable "${PB_CONSUMER_BINARY}/pbAudioStats_consumer")
if(WIN32)
    set(executable "${PB_CONSUMER_BINARY}/${PB_CONFIG}/pbAudioStats_consumer.exe")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Consumer executable failed: ${run_result}")
endif()
