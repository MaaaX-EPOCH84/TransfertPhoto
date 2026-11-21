add_executable(TransfertPhoto WIN32 MACOSX_BUNDLE
    main.cpp)

target_link_libraries(TransfertPhoto PRIVATE
    transfertphoto::core
    transfertphoto_warnings
    wx::core
    wx::base)

set_target_properties(TransfertPhoto PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_BUNDLE_BUNDLE_NAME "TransfertPhoto"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.maaax-epoch84.transfertphoto"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

if(WIN32)
    # The resource script brings the wxWidgets manifest: themed controls and
    # per-monitor DPI awareness.
    target_sources(TransfertPhoto PRIVATE app.rc)
    target_compile_definitions(TransfertPhoto PRIVATE wxUSE_DPI_AWARE_MANIFEST=2)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # GTK stays a system dependency; the C++ runtime does not have to.
    target_link_options(TransfertPhoto PRIVATE -static-libstdc++ -static-libgcc)
endif()
