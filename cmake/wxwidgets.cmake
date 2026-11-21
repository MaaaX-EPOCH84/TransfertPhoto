# wxWidgets, built from source as static libraries with the project.
#
# The release archive is used rather than a git checkout because it already
# contains the bundled third-party sources (zlib, libpng, libjpeg, ...), which
# are git submodules upstream.

include(FetchContent)

set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)

# Bundled copies everywhere: a system copy would become a runtime dependency
# of the single-file executable.
foreach(lib IN ITEMS ZLIB EXPAT LIBJPEG LIBPNG LIBTIFF REGEX NANOSVG)
    set(wxUSE_${lib} builtin CACHE STRING "" FORCE)
endforeach()

# Not needed by the application; each of these costs build time, and several
# pull in system libraries. wxHTML stays: the built-in help controller, enabled
# by default, refuses to compile without it.
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
