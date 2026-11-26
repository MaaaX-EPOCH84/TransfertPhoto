# Exiv2, pour les métadonnées des photos (date de prise de vue, plus tard les
# vignettes intégrées). Licence GPL-2.0-or-later, compatible avec la nôtre.
#
# Étiquette Git précise, comme Catch2 : le projet ne publie pas d'archive des
# sources, et celle que GitHub génère n'a pas d'empreinte garantie stable.
# À inclure après cmake/zlib.cmake.

include(FetchContent)

# Le CMake d'Exiv2 déclare un minimum ancien (3.11) : ses option() effaceraient
# une simple variable, d'où le cache forcé.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# PNG gardé (il faut zlib) ; BMFF pour le HEIF et le CR3.
set(EXIV2_ENABLE_PNG ON CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_BMFF ON CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_FILESYSTEM_ACCESS ON CACHE BOOL "" FORCE)

# Sans XMP ni vidéo : il faudrait expat, et la date des vidéos est lue par nos
# propres lecteurs. Le reste ne sert pas, ou tirerait une bibliothèque de plus
# (brotli, inih, curl, gettext).
foreach(option IN ITEMS
        XMP VIDEO BROTLI INIH NLS WEBREADY CURL LENSDATA DYNAMIC_RUNTIME)
    set(EXIV2_ENABLE_${option} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(option IN ITEMS EXIV2_COMMAND SAMPLES UNIT_TESTS FUZZ_TESTS DOC)
    set(EXIV2_BUILD_${option} OFF CACHE BOOL "" FORCE)
endforeach()

# Sous macOS, Exiv2 se lierait à la libiconv du système, une bibliothèque
# dynamique de plus. Elle ne sert qu'à convertir des commentaires, pas les
# dates ; sous Linux et Windows il s'en passe déjà.
set(CMAKE_DISABLE_FIND_PACKAGE_Iconv ON)

# Minimum ancien encore : sans cela, le choix du runtime de MSVC ne passe pas
# par CMAKE_MSVC_RUNTIME_LIBRARY.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

FetchContent_Declare(exiv2
    GIT_REPOSITORY https://github.com/Exiv2/exiv2.git
    GIT_TAG v0.28.9
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(exiv2)

unset(CMAKE_POLICY_DEFAULT_CMP0091)
unset(CMAKE_DISABLE_FIND_PACKAGE_Iconv)

# Exiv2 n'est pas propre sous GCC avec -Wextra : une construction du projet
# avec CMAKE_COMPILE_WARNING_AS_ERROR ne doit pas échouer sur son code.
set_target_properties(exiv2lib exiv2lib_int PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Exiv2 écrit exv_conf.h et exiv2lib_export.h à la racine de la construction,
# et ne le dit pas à ceux qui l'emploient. On passe par une cible à nous.
add_library(transfertphoto_exiv2 INTERFACE)
target_link_libraries(transfertphoto_exiv2 INTERFACE Exiv2::exiv2lib)
target_include_directories(transfertphoto_exiv2 SYSTEM INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>")
add_library(transfertphoto::exiv2 ALIAS transfertphoto_exiv2)
