include(FetchContent)

# Catch2, compilé avec le projet. Étiquette précise : une branche rendrait la
# construction non reproductible. Git est requis (installé dans la CI).
# Catch2 doit être compilé dans la même norme que les tests : en C++14, sous
# MSVC et Apple Clang, il n'affiche pas les std::string_view, et l'édition de
# liens échoue. La portée est celle de ce répertoire.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(Catch2)

# Fournit catch_discover_tests().
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

add_executable(transfertphoto_tests
    directory_source_test.cpp
    media_format_test.cpp
    media_signature_test.cpp
    version_test.cpp)
target_link_libraries(transfertphoto_tests PRIVATE
    transfertphoto::core
    transfertphoto_warnings
    Catch2::Catch2WithMain)
target_compile_definitions(transfertphoto_tests
    PRIVATE EXPECTED_VERSION="${PROJECT_VERSION}")

# Un test CTest par cas Catch2 : un échec se lit par son nom dans la CI.
catch_discover_tests(transfertphoto_tests)
