# Opt-in, synthetic-only T6.1 workbench. Not linked into the production application.
add_library(MmsPrototypeWidgets STATIC
    MmsPrototypeSession.cpp MmsPrototypeSession.hpp
    MmsPrototypeWindow.cpp MmsPrototypeWindow.hpp
    ${PROJECT_SOURCE_DIR}/src/gui/SclTableInteraction.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/SclTableInteraction.hpp)
target_include_directories(MmsPrototypeWidgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${PROJECT_SOURCE_DIR}/src/gui)
target_link_libraries(MmsPrototypeWidgets PUBLIC Qt${QT_VERSION_MAJOR}::Widgets
    ads::qtadvanceddocking-qt${QT_VERSION_MAJOR})

add_executable(MmsInteractionPrototype MmsPrototypeMain.cpp)
target_link_libraries(MmsInteractionPrototype PRIVATE MmsPrototypeWidgets)
set_target_properties(MmsInteractionPrototype PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(WIN32)
    add_dependencies(MmsInteractionPrototype DeployRuntime)
    add_custom_command(TARGET MmsInteractionPrototype POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:MmsInteractionPrototype>
            $<TARGET_FILE_DIR:MmsInteractionPrototype> COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING AND TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin)
    add_executable(MmsPrototypeTest ../integration/MmsPrototypeTest.cpp)
    target_link_libraries(MmsPrototypeTest PRIVATE MmsPrototypeWidgets Qt${QT_VERSION_MAJOR}::Test)
    target_include_directories(MmsPrototypeTest PRIVATE ${PROJECT_SOURCE_DIR}/src/gui)
    set_target_properties(MmsPrototypeTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(WIN32)
        add_dependencies(MmsPrototypeTest DeployRuntime)
        add_custom_command(TARGET MmsPrototypeTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:MmsPrototypeTest>
                $<TARGET_FILE_DIR:MmsPrototypeTest>
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MmsPrototypeTest>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:MmsPrototypeTest>/platforms/" COMMAND_EXPAND_LISTS)
    endif()
    add_test(NAME MmsPrototypeTest COMMAND MmsPrototypeTest)
    set_tests_properties(MmsPrototypeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()
