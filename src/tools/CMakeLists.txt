add_subdirectory(uic)
if (QT_FEATURE_dbus)
    add_subdirectory(qdbuscpp2xml)
    add_subdirectory(qdbusxml2cpp)
endif()
add_subdirectory(qlalr)
add_subdirectory(qvkgen)

# Only include the following tools when performing a host build
if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(androiddeployqt)
    if(QT_FEATURE_gui)
        add_subdirectory(androidtestrunner)
    endif()
endif()
