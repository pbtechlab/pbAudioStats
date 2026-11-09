add_executable(pbAudioStats_regression regression_tests.cpp)
pb_apply_msvc_runtime(pbAudioStats_regression)
target_link_libraries(pbAudioStats_regression PRIVATE pbAudioStatsLib)
target_include_directories(pbAudioStats_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib/src)
target_compile_features(pbAudioStats_regression PRIVATE cxx_std_17)

add_test(NAME pbAudioStats.regression COMMAND pbAudioStats_regression)

if(TARGET pbAudioStats)
    add_test(NAME pbAudioStats.cli_help COMMAND pbAudioStats -h)
endif()

set(PB_CONSUMER_COMMON_ARGS
    -DPB_AUDIO_STATS_SOURCE=${PROJECT_SOURCE_DIR}
    -DPB_AUDIO_STATS_BINARY=${PROJECT_BINARY_DIR}
    -DPB_CONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
    -DPB_GENERATOR=${CMAKE_GENERATOR}
    -DPB_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
    -DPB_CONFIG=$<CONFIG>
)

add_test(NAME pbAudioStats.consumer_add_subdirectory
    COMMAND ${CMAKE_COMMAND} ${PB_CONSUMER_COMMON_ARGS}
        -DPB_MODE=add_subdirectory
        -DPB_CONSUMER_BINARY=${CMAKE_CURRENT_BINARY_DIR}/consumer-add
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_consumer_test.cmake)

add_test(NAME pbAudioStats.consumer_install
    COMMAND ${CMAKE_COMMAND} ${PB_CONSUMER_COMMON_ARGS}
        -DPB_MODE=install
        -DPB_CONSUMER_BINARY=${CMAKE_CURRENT_BINARY_DIR}/consumer-install
        -DPB_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/install-prefix
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_consumer_test.cmake)
