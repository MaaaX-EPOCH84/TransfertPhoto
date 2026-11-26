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
    companion_test.cpp
    dating_test.cpp
    destination_test.cpp
    duplicate_decisions_test.cpp
    duplicate_situations_test.cpp
    duplicates_test.cpp
    directory_source_test.cpp
    entry_filter_test.cpp
    exif_samples.hpp
    exiv2_test.cpp
    file_hash_test.cpp
    implausible_date_test.cpp
    inventory_test.cpp
    local_date_time_test.cpp
    media_format_test.cpp
    media_signature_test.cpp
    memory_source.hpp
    name_date_test.cpp
    photo_date_test.cpp
    source_io_test.cpp
    test_files.hpp
    version_test.cpp
    video_date_test.cpp
    video_samples.hpp)
target_link_libraries(transfertphoto_tests PRIVATE
    transfertphoto::core
    transfertphoto::exiv2
    transfertphoto_warnings
    Catch2::Catch2WithMain)
target_compile_definitions(transfertphoto_tests
    PRIVATE EXPECTED_VERSION="${PROJECT_VERSION}")

# Un test CTest par cas Catch2 : un échec se lit par son nom dans la CI.
catch_discover_tests(transfertphoto_tests)
