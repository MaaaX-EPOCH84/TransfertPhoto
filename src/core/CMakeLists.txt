# La logique de l'application, sans aucune dépendance graphique, pour être testée seule.
add_library(transfertphoto_core STATIC
    version.cpp
    version.hpp)
add_library(transfertphoto::core ALIAS transfertphoto_core)

target_compile_features(transfertphoto_core PUBLIC cxx_std_20)
target_include_directories(transfertphoto_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(transfertphoto_core
    PRIVATE TRANSFERTPHOTO_VERSION="${PROJECT_VERSION}")
target_link_libraries(transfertphoto_core PRIVATE transfertphoto_warnings)
