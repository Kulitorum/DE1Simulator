QT = core bluetooth network
CONFIG += console c++17
CONFIG -= app_bundle

TARGET = de1-ble-daemon
SOURCES += de1-ble-daemon.cpp

# For BlueZ peripheral support on Linux
linux {
    CONFIG += link_pkgconfig
}
