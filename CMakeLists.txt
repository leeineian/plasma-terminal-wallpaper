cmake_minimum_required(VERSION 3.16)
project("plasma-terminal-wallpaper")

option(INSTALL_WALLPAPER "Install wallpaper" ON)

# Enable automatic moc compilation
set(CMAKE_AUTOMOC ON)

# Find Qt6 components (minimal requirements)
find_package(Qt6 COMPONENTS Core Quick Qml REQUIRED)

# Find libvterm via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED vterm)



# Get id and version from metadata
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/package/metadata.json METADATA)
string(JSON PLUGIN_ID GET ${METADATA} KPlugin Id)
string(JSON CUR_VERSION GET ${METADATA} KPlugin Version)
message("Plugin Id: ${PLUGIN_ID}")
message("Version: ${CUR_VERSION}")

# Define the C++ QML plugin
add_library(term_plugin SHARED
    src/plugin.cpp
    src/terminal.cpp
    src/terminal.h
)

target_include_directories(term_plugin PRIVATE ${VTERM_INCLUDE_DIRS})

target_link_libraries(term_plugin
    Qt6::Core
    Qt6::Quick
    Qt6::Qml
    ${VTERM_LIBRARIES}
)

# Set plugin properties (matching qmldir expectations)
set_target_properties(term_plugin PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

if(INSTALL_WALLPAPER)
    # Define installation destination path for wallpaper packages
    set(WALLPAPER_INSTALL_DIR "share/plasma/wallpapers/${PLUGIN_ID}")
    
    # Install the QML package files (metadata.json, contents/ config & ui QMLs)
    install(DIRECTORY package/ DESTINATION ${WALLPAPER_INSTALL_DIR})
    
    # Install the compiled C++ plugin directly into the contents/ui folder of the installed package
    install(TARGETS term_plugin DESTINATION ${WALLPAPER_INSTALL_DIR}/contents/ui)
endif()

