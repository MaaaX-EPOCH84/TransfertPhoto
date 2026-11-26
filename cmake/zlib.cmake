# zlib, une seule copie pour tout le projet.
#
# wxWidgets (PNG, TIFF, flux compressés) et Exiv2 (PNG) en ont besoin tous les
# deux. Deux zlib liées en statique dans le même exécutable exportent les mêmes
# symboles : l'éditeur de liens en garde une sans prévenir, ou refuse. On la
# construit donc une fois, et chaque find_package(ZLIB) qui suit la retrouve
# au lieu d'aller chercher celle du système.

include(FetchContent)

set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
# L'installation reste déclarée : Exiv2 exporte sa cible, et CMake exige que
# chaque bibliothèque dont elle dépend appartienne elle aussi à un export.
# Rien n'est installé, le projet n'a pas de règle d'installation.
set(ZLIB_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(ZLIB
    URL https://github.com/madler/zlib/releases/download/v1.4/zlib-1.4.tar.gz
    URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
    SYSTEM
    EXCLUDE_FROM_ALL
    OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(ZLIB)

# Le code tiers garde ses avertissements pour lui, même quand le projet est
# construit avec CMAKE_COMPILE_WARNING_AS_ERROR.
set_target_properties(zlibstatic PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# wxWidgets, pour écrire son script wx-config, lit l'OUTPUT_NAME de chaque
# bibliothèque dont il dépend, et à défaut leur LOCATION, que CMake refuse de
# donner pour une cible construite. zlib ne pose que ARCHIVE_OUTPUT_NAME, qui
# garde la main sur le nom du fichier.
set_target_properties(zlibstatic PROPERTIES OUTPUT_NAME z)

# zlib ne nomme ZLIB::ZLIB que sa version partagée.
add_library(ZLIB::ZLIB ALIAS zlibstatic)

# Les variables du module FindZLIB, que wxWidgets lit à la place de la cible.
# Ce fichier est relu par chaque find_package(ZLIB) redirigé vers cette copie.
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/zlib-extra.cmake" "
set(ZLIB_FOUND TRUE)
set(ZLIB_VERSION \"1.4\")
set(ZLIB_VERSION_STRING \"1.4\")
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIR \"${zlib_BINARY_DIR};${zlib_SOURCE_DIR}\")
set(ZLIB_INCLUDE_DIRS \"${zlib_BINARY_DIR};${zlib_SOURCE_DIR}\")
")
