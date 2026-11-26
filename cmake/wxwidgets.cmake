# wxWidgets, compilé depuis ses sources en bibliothèques statiques avec le projet.
#
# L'archive de release est préférée à un clone Git parce qu'elle contient déjà
# les sources tierces embarquées (zlib, libpng, libjpeg...), qui sont des
# sous-modules Git en amont.

include(FetchContent)

set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)

# Copies embarquées partout : une copie du système deviendrait une dépendance
# à l'exécution de l'exécutable unique.
foreach(lib IN ITEMS EXPAT LIBJPEG LIBPNG LIBTIFF REGEX NANOSVG)
    set(wxUSE_${lib} builtin CACHE STRING "" FORCE)
endforeach()

# Sauf zlib : « sys » lui fait appeler find_package(ZLIB), qui trouve la copie
# commune de cmake/zlib.cmake, partagée avec Exiv2.
set(wxUSE_ZLIB sys CACHE STRING "" FORCE)

# Inutiles à l'application ; chacun coûte du temps de compilation, et plusieurs
# tirent des bibliothèques du système. wxHTML reste : l'aide intégrée, active
# par défaut, refuse de compiler sans lui.
foreach(feature IN ITEMS
        STC WEBVIEW MEDIACTRL RICHTEXT RIBBON PROPGRID AUI XRC
        OPENGL WEBREQUEST LIBLZMA LIBSDL LIBNOTIFY LIBMSPACK LIBGNOMEVFS)
    set(wxUSE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()

FetchContent_Declare(wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.tar.bz2
    URL_HASH SHA256=a62719bb5e1dcc41c1a6fc1eecd499c7ee8521f70402c7b1bb3342a1e16344e0
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(wxWidgets)

# wxWidgets ne trouvant pas zlib reprendrait sa propre copie sans rien dire,
# et l'exécutable en contiendrait deux.
if(TARGET wxzlib)
    message(FATAL_ERROR "wxWidgets a pris son zlib embarqué au lieu de la copie commune")
endif()
