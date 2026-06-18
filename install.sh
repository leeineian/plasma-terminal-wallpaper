#!/bin/sh

set -e

if [ "$(whoami)" = root ];
then
    echo "Please do not run this script as root or using sudo."
    exit 1
fi

if [ -d "build" ]; then
    rm -rf build
fi

echo "Configuring build with CMake..."
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=$HOME/.local

echo "Building C++ plugin..."
cmake --build build

echo "Installing plugin and wallpaper package..."
cmake --install build

echo "Installation complete! Please restart plasmashell if needed using:"
echo "systemctl --user restart plasma-plasmashell.service"
