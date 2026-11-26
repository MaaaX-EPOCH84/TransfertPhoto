# La logique de l'application, sans aucune dépendance graphique, pour être testée seule.
add_library(transfertphoto_core STATIC
    companion.cpp
    companion.hpp
    dating.cpp
    dating.hpp
    destination.cpp
    destination.hpp
    duplicate_situations.cpp
    duplicate_situations.hpp
    duplicates.cpp
    duplicates.hpp
    directory_source.cpp
    directory_source.hpp
    entry_filter.cpp
    entry_filter.hpp
    file_hash.cpp
    file_hash.hpp
    implausible_date.cpp
    implausible_date.hpp
    inventory.cpp
    inventory.hpp
    local_date_time.cpp
    local_date_time.hpp
    media_format.cpp
    media_format.hpp
    media_signature.cpp
    media_signature.hpp
    name_date.cpp
    name_date.hpp
    photo_date.cpp
    photo_date.hpp
    source.hpp
    source_io.cpp
    source_io.hpp
    version.cpp
    version.hpp
    video_date.cpp
    video_date.hpp)
add_library(transfertphoto::core ALIAS transfertphoto_core)

target_compile_features(transfertphoto_core PUBLIC cxx_std_20)
target_include_directories(transfertphoto_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(transfertphoto_core
    PRIVATE TRANSFERTPHOTO_VERSION="${PROJECT_VERSION}")
# Exiv2 en PUBLIC : source_io.hpp expose ses types.
target_link_libraries(transfertphoto_core
    PUBLIC transfertphoto::exiv2
    PRIVATE transfertphoto::xxhash transfertphoto_warnings)
