cmake_minimum_required(VERSION 3.21)

project(IEC61850Simulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Network Test Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Network Test Widgets)

# Keep ADS version discovery independent of the vendored source directory's Git state.
set(ADS_VERSION "5.0.0" CACHE STRING "Qt Advanced Docking System version" FORCE)
set(BUILD_STATIC OFF CACHE BOOL "Build QtADS as a static library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build QtADS examples" FORCE)
add_subdirectory(third_party/Qt-Advanced-Docking-System-5.0.0)

set(AppTarget IEC61850Simulator)
add_executable(${AppTarget}
    app/ui/Main.cpp
    src/core/RedisAvailabilityChecker.cpp
    src/core/RedisAvailabilityChecker.hpp
    src/core/ApplicationLogger.cpp
    src/core/ApplicationLogger.hpp
    src/core/RedisConfiguration.cpp
    src/core/RedisConfiguration.hpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.hpp
)

target_include_directories(${AppTarget}
    PRIVATE
        src/core
        src/gui
)

target_link_libraries(${AppTarget}
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Widgets
        ads::qtadvanceddocking-qt${QT_VERSION_MAJOR}
)

set_target_properties(${AppTarget} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET ${AppTarget} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSourcePath="${CMAKE_CURRENT_SOURCE_DIR}/config/Redis.json"
        -DDestinationPath="$<TARGET_FILE_DIR:${AppTarget}>/config/Redis.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDefaultRuntimeConfig.cmake"
    COMMENT "Creating the default Redis configuration when it is missing"
)

# ADS builds its DLL in its own output directory. Deploy all runtime DLLs known
# to CMake alongside the executable; do not use tools/QtADS as a runtime path.
if(WIN32)
    add_custom_command(TARGET ${AppTarget} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${AppTarget}>
            $<TARGET_FILE_DIR:${AppTarget}>
        COMMAND_EXPAND_LISTS
        COMMENT "Deploying runtime DLLs next to ${AppTarget}"
    )
endif()

include(GNUInstallDirs)
install(TARGETS ${AppTarget}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES $<TARGET_FILE:qtadvanceddocking-qt${QT_VERSION_MAJOR}>
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    OPTIONAL
)
install(FILES config/Redis.json DESTINATION ${CMAKE_INSTALL_BINDIR}/config)

include(CTest)
if(BUILD_TESTING)
    add_executable(RedisConfigurationTest
        tests/unit/RedisConfigurationTest.cpp
        src/core/RedisConfiguration.cpp
        src/core/RedisConfiguration.hpp
    )
    target_include_directories(RedisConfigurationTest PRIVATE src/core)
    target_link_libraries(RedisConfigurationTest PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME RedisConfigurationTest COMMAND RedisConfigurationTest)
endif()
