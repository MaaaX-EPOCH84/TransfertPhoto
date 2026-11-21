add_executable(transfertphoto_tests
    version_test.cpp)
target_link_libraries(transfertphoto_tests PRIVATE
    transfertphoto::core
    transfertphoto_warnings)
target_compile_definitions(transfertphoto_tests
    PRIVATE EXPECTED_VERSION="${PROJECT_VERSION}")

add_test(NAME transfertphoto_tests COMMAND transfertphoto_tests)
