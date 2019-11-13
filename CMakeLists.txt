cmake_minimum_required(VERSION 3.15.0)

# Run auto detection routines
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QtAutoDetect.cmake)

project(QtBase
    VERSION 6.0.0
    DESCRIPTION "Qt Base Libraries"
    HOMEPAGE_URL "https://qt.io/"
    LANGUAGES CXX C ASM
)

if(NOT QT_BUILD_STANDALONE_TESTS)
    ## Add some paths to check for cmake modules:
    list(PREPEND CMAKE_MODULE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/extra-cmake-modules/find-modules"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/kwin"
    )

    ## Find the build internals package.
    set(QT_BUILD_INTERNALS_SKIP_CMAKE_MODULE_PATH_ADDITION TRUE)
    list(PREPEND CMAKE_PREFIX_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    )
    find_package(QtBuildInternals CMAKE_FIND_ROOT_PATH_BOTH)
    unset(QT_BUILD_INTERNALS_SKIP_CMAKE_MODULE_PATH_ADDITION)
else()
    # When building standalone tests, an istalled BuildInternals package already exists.
    find_package(Qt6 REQUIRED COMPONENTS BuildInternals CMAKE_FIND_ROOT_PATH_BOTH)
endif()

qt_build_repo_begin()

if(NOT QT_BUILD_STANDALONE_TESTS)
    ## Should this Qt be static or dynamically linked?
    option(BUILD_SHARED_LIBS "Build Qt statically or dynamically" ON)
    set(QT_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})

    ## Should this Qt be built with Werror?
    option(WARNINGS_ARE_ERRORS "Build Qt with warnings as errors" ${FEATURE_developer_build})

    ## Should Qt be built using PCH?
    option(BUILD_WITH_PCH "Build Qt using precompiled headers?" ON)

    ## QtBase specific configure tests:
    include(QtBaseConfigureTests)

    ## Build System tests:
    include(QtBaseCMakeTesting)

    ## Targets for global features, etc.:
    include(QtBaseGlobalTargets)

    ## Set language standards after QtBaseGlobalTargets, because that's when the relevant
    ## feature variables are available.
    qt_set_language_standards()

    ## Visit all the directories:
    add_subdirectory(src)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
    if(QT_NO_MAKE_TESTS)
        set_property(DIRECTORY tests PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endif()

if(NOT QT_BUILD_STANDALONE_TESTS)
    if(QT_WILL_BUILD_TOOLS)
        add_subdirectory(qmake)
    endif()
    # As long as we use the mkspecs (for qplatformdefs.h), we need to always
    # install it, especially when cross-compiling.
    set(mkspecs_install_dir "${INSTALL_DATADIR}")
    qt_path_join(mkspecs_install_dir ${QT_INSTALL_DIR} ${mkspecs_install_dir})

    qt_copy_or_install(DIRECTORY "${PROJECT_SOURCE_DIR}/mkspecs"
                       DESTINATION ${mkspecs_install_dir})
endif()

qt_build_repo_end()

if(NOT QT_BUILD_STANDALONE_TESTS AND BUILD_EXAMPLES)
    add_subdirectory(examples)
    if(QT_NO_MAKE_EXAMPLES)
        set_property(DIRECTORY examples PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endif()