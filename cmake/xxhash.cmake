# xxHash, pour les empreintes des doublons. Licence BSD-2-Clause pour la
# bibliothèque (l'outil xxhsum, GPL, n'est pas employé).
#
# Étiquette Git précise : le projet ne publie pas d'archive des sources. Rien
# n'est compilé à part : l'en-tête s'emploie avec XXH_INLINE_ALL, dans le seul
# fichier qui calcule les empreintes.

include(FetchContent)

FetchContent_Declare(xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.4
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL)
# Le dépôt n'a pas de CMakeLists.txt à sa racine : seules les sources arrivent.
FetchContent_MakeAvailable(xxhash)

add_library(transfertphoto_xxhash INTERFACE)
target_include_directories(transfertphoto_xxhash SYSTEM INTERFACE "${xxhash_SOURCE_DIR}")
add_library(transfertphoto::xxhash ALIAS transfertphoto_xxhash)
