cmake_minimum_required(VERSION 3.21)

project(IEC61850Simulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Concurrent Core Gui Network Test Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent Core Gui Network Test Widgets)

# Scope upstream's generic options to this dependency; ADS remains a DLL.
function(AddHiredis)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(DISABLE_TESTS ON)
    set(ENABLE_EXAMPLES OFF)
    set(ENABLE_SSL OFF)
    set(ENABLE_NUGET OFF)
    set(CMAKE_AUTOMOC OFF)
    add_subdirectory(third_party/hiredis-1.3.0)
endfunction()
AddHiredis()

add_library(SclAnalysis STATIC
    src/iec61850/scl/SclDocument.hpp
    src/iec61850/scl/SclReader.cpp
    src/iec61850/scl/SclReader.hpp
    src/iec61850/scl/SclReferenceResolver.cpp
    src/iec61850/scl/SclReferenceResolver.hpp
    src/iec61850/scl/SclSignalCatalog.cpp
    src/iec61850/scl/SclSignalCatalog.hpp
    src/iec61850/scl/SclSourceIndex.cpp
    src/iec61850/scl/SclSourceIndex.hpp
    src/iec61850/scl/SclTaskControl.hpp)
target_include_directories(SclAnalysis PUBLIC src/iec61850/scl)
target_link_libraries(SclAnalysis PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(SclApplication STATIC src/core/SclAnalysisService.cpp src/core/SclAnalysisService.hpp
    src/core/SclConnections.cpp src/core/SclConnections.hpp
    src/core/SclTaskIndex.cpp src/core/SclTaskIndex.hpp src/core/SclTaskQuery.cpp src/core/SclTaskDetails.cpp
    src/core/SclBrowseIndex.cpp src/core/SclBrowseIndex.hpp
    src/core/SclSearch.cpp src/core/SclSearch.hpp
    src/core/SclIssueExport.cpp src/core/SclIssueExport.hpp
    src/core/SclTopology.cpp src/core/SclTopology.hpp src/core/SclTopologyResources.qrc)
set_target_properties(SclApplication PROPERTIES AUTORCC ON)
target_include_directories(SclApplication PUBLIC src/core)
target_link_libraries(SclApplication PUBLIC SclAnalysis PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent)

add_library(RedisClient STATIC src/core/RedisClient.cpp src/core/RedisClient.hpp)
target_include_directories(RedisClient PUBLIC src/core)
target_link_libraries(RedisClient PUBLIC Qt${QT_VERSION_MAJOR}::Core
    PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent hiredis::hiredis)

# Keep ADS version discovery independent of the vendored source directory's Git state.
set(ADS_VERSION "5.0.0" CACHE STRING "Qt Advanced Docking System version" FORCE)
set(BUILD_STATIC OFF CACHE BOOL "Build QtADS as a static library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build QtADS examples" FORCE)
add_subdirectory(third_party/Qt-Advanced-Docking-System-5.0.0)

set(AppTarget IEC61850Simulator)
add_executable(${AppTarget}
    app/ui/Main.cpp
    res/Application.qrc
    src/core/RedisAvailabilityChecker.cpp
    src/core/RedisAvailabilityChecker.hpp
    src/core/ApplicationLogger.cpp
    src/core/ApplicationLogger.hpp
    src/core/RedisConfiguration.cpp
    src/core/RedisConfiguration.hpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.hpp
    src/gui/SclWorkspace.cpp
    src/gui/SclWorkspace.hpp
    src/gui/SclTaskPages.cpp src/gui/SclTaskPages.hpp
    src/gui/SclTaskBrowser.cpp src/gui/SclTaskBrowser.hpp
    src/gui/SclConnectionView.cpp src/gui/SclConnectionView.hpp
    src/gui/SclSourcePreview.cpp
    src/gui/SclSourcePreview.hpp
    src/gui/SclWorkspaceTopology.cpp
    src/gui/SclSearchSession.cpp
    src/gui/SclSearchSession.hpp
    src/gui/SclWorkspaceSearch.cpp
)

set_target_properties(${AppTarget} PROPERTIES AUTORCC ON)
if(WIN32)
    enable_language(RC)
    target_sources(${AppTarget} PRIVATE res/Application.rc)
endif()

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
        Qt${QT_VERSION_MAJOR}::Concurrent
        RedisClient
        SclApplication
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

    # TARGET_RUNTIME_DLLS does not include Qt plugins or third-party DLLs
    # imported by prebuilt Qt/MinGW binaries. Use the selected Qt installation
    # and compiler, never another toolchain found on PATH.
    get_target_property(QtCoreLocation Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QtCoreLocation)
        get_target_property(QtCoreLocation Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QtRuntimeDirectory "${QtCoreLocation}" DIRECTORY)
    get_filename_component(CompilerRuntimeDirectory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(QtDeployTool NAMES windeployqt6.exe windeployqt.exe
        HINTS "${QtRuntimeDirectory}" NO_DEFAULT_PATH REQUIRED)
    add_custom_target(DeployRuntime ALL
        COMMAND "${CMAKE_COMMAND}"
            "-DApplicationPath=$<TARGET_FILE:${AppTarget}>"
            "-DQtDeployTool=${QtDeployTool}"
            "-DQtRuntimeDirectory=${QtRuntimeDirectory}"
            "-DCompilerRuntimeDirectory=${CompilerRuntimeDirectory}"
            "-DCMAKE_OBJDUMP=${CMAKE_OBJDUMP}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindowsRuntime.cmake"
        DEPENDS ${AppTarget}
        COMMENT "Deploying Qt plugins and transitive Windows runtime dependencies"
        VERBATIM)
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
install(FILES third_party/hiredis-1.3.0/COPYING DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/hiredis)

include(CTest)
if(BUILD_TESTING)
    add_executable(SclReaderTest tests/unit/SclReaderTest.cpp)
    target_link_libraries(SclReaderTest PRIVATE SclAnalysis Qt${QT_VERSION_MAJOR}::Test)
    target_compile_definitions(SclReaderTest PRIVATE SCL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scl")
    add_test(NAME SclReaderTest COMMAND SclReaderTest)

    add_executable(SclReferenceResolverTest tests/unit/SclReferenceResolverTest.cpp)
    target_link_libraries(SclReferenceResolverTest PRIVATE SclAnalysis Qt${QT_VERSION_MAJOR}::Test)
    target_compile_definitions(SclReferenceResolverTest PRIVATE SCL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scl")
    add_test(NAME SclReferenceResolverTest COMMAND SclReferenceResolverTest)

    add_executable(SclTaskIndexTest tests/unit/SclTaskIndexTest.cpp)
    target_link_libraries(SclTaskIndexTest PRIVATE SclApplication Qt${QT_VERSION_MAJOR}::Test)
    target_compile_definitions(SclTaskIndexTest PRIVATE SCL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scl")
    add_test(NAME SclTaskIndexTest COMMAND SclTaskIndexTest)

    # Local performance/scale probe; CTest does not require an external SCD.
    add_executable(SclAnalysisProbe tests/integration/SclAnalysisProbe.cpp)
    target_link_libraries(SclAnalysisProbe PRIVATE SclAnalysis)
    if(WIN32)
        target_link_libraries(SclAnalysisProbe PRIVATE psapi)
    endif()

    add_executable(SclFixtureBaselineTest tests/unit/SclFixtureBaselineTest.cpp)
    target_link_libraries(SclFixtureBaselineTest PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
    target_compile_definitions(SclFixtureBaselineTest PRIVATE SCL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scl")
    add_test(NAME SclFixtureBaselineTest COMMAND SclFixtureBaselineTest)

    if(TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin)
        add_executable(SclTaskPagesTest tests/integration/SclTaskPagesTest.cpp
            src/gui/SclConnectionView.cpp src/gui/SclConnectionView.hpp
            src/gui/SclTaskPages.cpp src/gui/SclTaskPages.hpp)
        target_include_directories(SclTaskPagesTest PRIVATE src/gui)
        target_link_libraries(SclTaskPagesTest PRIVATE SclApplication Qt${QT_VERSION_MAJOR}::Concurrent
            Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME SclTaskPagesTest COMMAND SclTaskPagesTest)
        set_tests_properties(SclTaskPagesTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(MainWindowBaselineTest
            res/Application.qrc
            tests/integration/MainWindowBaselineTest.cpp
            src/gui/SclConnectionView.cpp src/gui/SclConnectionView.hpp
            src/gui/SclTaskPages.cpp src/gui/SclTaskPages.hpp
            src/gui/SclTaskBrowser.cpp src/gui/SclTaskBrowser.hpp
            src/gui/MainWindow.cpp src/gui/MainWindow.hpp src/gui/SclWorkspace.cpp src/gui/SclWorkspace.hpp
            src/gui/SclSearchSession.cpp src/gui/SclSearchSession.hpp src/gui/SclWorkspaceSearch.cpp
            src/gui/SclSourcePreview.cpp src/gui/SclSourcePreview.hpp src/gui/SclWorkspaceTopology.cpp)
        target_include_directories(MainWindowBaselineTest PRIVATE src/gui)
        target_link_libraries(MainWindowBaselineTest PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
            Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Concurrent SclApplication ads::qtadvanceddocking-qt${QT_VERSION_MAJOR})
        set_target_properties(MainWindowBaselineTest PROPERTIES AUTORCC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        target_compile_definitions(MainWindowBaselineTest PRIVATE
            BASELINE_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/p0-artifacts"
            SCL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scl")
        if(WIN32)
            target_link_libraries(MainWindowBaselineTest PRIVATE psapi)
            add_custom_command(TARGET MainWindowBaselineTest POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:MainWindowBaselineTest>
                    $<TARGET_FILE_DIR:MainWindowBaselineTest>
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MainWindowBaselineTest>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:MainWindowBaselineTest>/platforms/"
                COMMAND_EXPAND_LISTS)
        endif()
        add_test(NAME MainWindowBaselineTest COMMAND MainWindowBaselineTest)
        set_tests_properties(MainWindowBaselineTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    endif()

    add_executable(RedisClientTest tests/unit/RedisClientTest.cpp)
    target_link_libraries(RedisClientTest PRIVATE RedisClient Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME RedisClientTest COMMAND RedisClientTest)
    set_tests_properties(RedisClientTest PROPERTIES TIMEOUT 20)

    if(WIN32 AND TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin)
        add_executable(RedisStartupTest tests/integration/RedisStartupTest.cpp)
        target_link_libraries(RedisStartupTest PRIVATE RedisClient Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
        add_dependencies(RedisStartupTest ${AppTarget})
        add_custom_command(TARGET RedisStartupTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${AppTarget}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:${AppTarget}>/platforms/"
        )
        add_test(NAME RedisStartupTest COMMAND RedisStartupTest
            "$<TARGET_FILE:${AppTarget}>" "${CMAKE_CURRENT_SOURCE_DIR}/tools/Redis/redis-server.exe")
        set_tests_properties(RedisStartupTest PROPERTIES TIMEOUT 40)
    endif()

    add_executable(RedisConfigurationTest
        tests/unit/RedisConfigurationTest.cpp
        src/core/RedisConfiguration.cpp
        src/core/RedisConfiguration.hpp
    )
    target_include_directories(RedisConfigurationTest PRIVATE src/core)
    target_link_libraries(RedisConfigurationTest PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME RedisConfigurationTest COMMAND RedisConfigurationTest)
endif()
