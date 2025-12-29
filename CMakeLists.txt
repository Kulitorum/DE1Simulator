cmake_minimum_required(VERSION 3.16)
project(DE1Simulator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(DE1Simulator WIN32
    main.cpp
)

target_link_libraries(DE1Simulator PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Windows deployment - use windeployqt to copy all required DLLs and plugins
if(WIN32)
    # Find windeployqt executable
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET DE1Simulator POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:DE1Simulator>"
            COMMENT "Running windeployqt to copy Qt dependencies..."
        )
    else()
        message(WARNING "windeployqt not found - Qt DLLs will not be copied automatically")
    endif()
endif()
