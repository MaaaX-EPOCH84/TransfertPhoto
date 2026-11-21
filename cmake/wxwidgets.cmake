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
foreach(lib IN ITEMS ZLIB EXPAT LIBJPEG LIBPNG LIBTIFF REGEX NANOSVG)
    set(wxUSE_${lib} builtin CACHE STRING "" FORCE)
endforeach()

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

# Le zlib embarqué n'inclut <unistd.h> que si son ./configure l'y autorise, et
# la construction CMake de wxWidgets ne le lance pas. Sans cela read(), write(),
# close() et lseek() restent non déclarés, ce que GCC refuse depuis sa version 14.
if(UNIX AND TARGET wxzlib)
    target_compile_definitions(wxzlib PRIVATE Z_HAVE_UNISTD_H)
endif()
