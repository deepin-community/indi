cmake_minimum_required(VERSION 3.13)
PROJECT(libindi C CXX)

# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake_modules/")
include(GNUInstallDirs)
include(FeatureSummary)

if(ANDROID OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Android")
    set(ANDROID ON)
    add_definitions(-DANDROID)
endif()

include(CMakeCommon)
include(CheckFunctionExists)

# Clang Format support
if(UNIX OR APPLE)
    set(FORMAT_CODE OFF CACHE BOOL "Enable Clang Format")

    if(FORMAT_CODE MATCHES ON)
        FILE(GLOB_RECURSE ALL_SOURCE_FILES *.c *.cpp *.h)

        FOREACH(SOURCE_FILE ${ALL_SOURCE_FILES})
            STRING(FIND ${SOURCE_FILE} ${CMAKE_SOURCE_DIR} DIR_FOUND)

            if(NOT ${DIR_FOUND} EQUAL 0)
                LIST(REMOVE_ITEM ALL_SOURCE_FILES ${SOURCE_FILE})
            endif()
        ENDFOREACH()

        FIND_PROGRAM(CLANGFORMAT_EXE NAMES clang-format-5.0)

        if(CLANGFORMAT_EXE)
            ADD_CUSTOM_TARGET(clang-format COMMAND ${CLANGFORMAT_EXE} -style=file -i ${ALL_SOURCE_FILES})
        endif()
    endif()
endif()

# ####################################  INDI version  ################################################
# N.B. DO NOT Forget to update version also in indiapi.h
# Proper way is to use indiversion.h.cmake file but this would break make existing applications so let us stick to the old proven way
set(INDI_SOVERSION "1")
set(CMAKE_INDI_VERSION_MAJOR 1)
set(CMAKE_INDI_VERSION_MINOR 9)
set(CMAKE_INDI_VERSION_RELEASE 9)
set(CMAKE_INDI_VERSION_STRING "${CMAKE_INDI_VERSION_MAJOR}.${CMAKE_INDI_VERSION_MINOR}.${CMAKE_INDI_VERSION_RELEASE}")
set(INDI_VERSION ${CMAKE_INDI_VERSION_MAJOR}.${CMAKE_INDI_VERSION_MINOR}.${CMAKE_INDI_VERSION_RELEASE})

execute_process(
    COMMAND git describe --tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GIT_TAG_RESULT
)

if(NOT ${GIT_TAG_RESULT} EQUAL 0)
    set(GIT_TAG "${CMAKE_INDI_VERSION_STRING}-tgz")
endif()

add_definitions(-DGIT_TAG_STRING=\"${GIT_TAG}\")

# #######################################  Paths  ###################################################
set(DATA_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/indi/")
set(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin")
set(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/include")

if(APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "-undefined dynamic_lookup")
endif(APPLE)

# #################################  Install Directories  ###########################################
# # the following are directories where stuff will be installed to
set(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/include/")
set(PKGCONFIG_INSTALL_PREFIX "${CMAKE_INSTALL_LIBDIR}/pkgconfig/")
set(UDEVRULES_INSTALL_DIR "/lib/udev/rules.d" CACHE STRING "Base directory for udev rules")

set(PKG_CONFIG_LIBDIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})

# ####################################  Build Options  ##############################################
# Select which components to build and what options to apply
OPTION(INDI_BUILD_SERVER "Build INDI Server" ON)
OPTION(INDI_BUILD_DRIVERS "Build INDI Drivers, Tools, and Examples" ON)
OPTION(INDI_BUILD_CLIENT "Build INDI POSIX Client" ON)
OPTION(INDI_BUILD_QT5_CLIENT "Build INDI Qt5 Client" OFF)
OPTION(INDI_BUILD_UNITTESTS "Build INDI tests" OFF)
OPTION(INDI_BUILD_INTEGTESTS "Build INDI integration tests" OFF)
OPTION(INDI_BUILD_WEBSOCKET "Build INDI with Websocket support" OFF)
OPTION(INDI_FAST_BLOB "Build INDI with Fast BLOB support" ON)
OPTION(INDI_BUILD_SHARED "Build shared library" ON)
OPTION(INDI_BUILD_STATIC "Build static library" ON)

if(UNIX AND NOT APPLE)
    OPTION(INDI_SHARED_MEMORY "Build INDI with support for UNIX protocol with shared memory" ON)
else()
    OPTION(INDI_SHARED_MEMORY "Build INDI with support for UNIX protocol with shared memory (require shm specific settings)" OFF)
endif()

OPTION(INDI_CALCULATE_MINMAX "Calculate and store image minimum and maximum values in FITS header" OFF)

CHECK_FUNCTION_EXISTS(mremap HAVE_MREMAP)

if(HAVE_MREMAP)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_MREMAP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_MREMAP")
endif()

# ##################################################################################################
# ########################################  Fast Blob  #############################################
# ##################################################################################################
if(INDI_FAST_BLOB)
    # Append ENCLEN attribute to outgoing BLOB elements to enable fast parsing by clients
    add_definitions(-DWITH_ENCLEN)
endif(INDI_FAST_BLOB)

# ##################################################################################################
# ###################################  UNIX protocol / SHM  ########################################
# ##################################################################################################
if(INDI_SHARED_MEMORY)
    if(APPLE)
        message(WARNING "Shared memory protocol require specific shared memory settings")
    else()
        add_definitions(-DENABLE_INDI_SHARED_MEMORY)
    endif()
endif()

# ##################################################################################################
# #####################################  Calculate Min/Max #########################################
# ##################################################################################################
if(INDI_CALCULATE_MINMAX)
    # Calculate Min/Max values to store them in FITS header
    add_definitions(-DWITH_MINMAX)
endif(INDI_CALCULATE_MINMAX)

# ##################################################################################################
# ####################################  Components  ################################################
# ##################################################################################################
set_package_properties(Nova PROPERTIES DESCRIPTION "A general purpose, double precision, Celestial Mechanics, Astrometry and Astrodynamics library" URL "http://libnova.sourceforge.net" TYPE REQUIRED PURPOSE "Provides INDI with astrodynamics library.")
set_package_properties(CFITSIO PROPERTIES DESCRIPTION "A library for reading and writing data files in FITS (Flexible Image Transport System) data format" URL "http://heasarc.gsfc.nasa.gov/fitsio/fitsio.html" TYPE REQUIRED PURPOSE "Provides INDI with FITS I/O support.")

# ##################################################################################################
# ####################################  Common Files  ##############################################
# ##################################################################################################
include_directories(libs)
install(FILES
    libs/indimacros.h
    DESTINATION
    ${INCLUDE_INSTALL_DIR}/libindi
    COMPONENT Devel
)

# ###################################################################################################
#
# Component   : INDI Core
# Dependencies:
# Supported OS: All
#
# ##################################################################################################
add_subdirectory(libs/indicore)

# ##################################################################################################
#
# Component   : indidevice
# Dependencies: indicore
# Supported OS: All
#
# ##################################################################################################
add_subdirectory(libs/indidevice)

# ##################################################################################################
#
# Component   : indiabstractclient
# Dependencies: indicore, indidevice
# Supported OS: All
#
# ##################################################################################################
if(INDI_BUILD_CLIENT OR INDI_BUILD_QT5_CLIENT)
    add_subdirectory(libs/indiabstractclient)
endif()

# ##################################################################################################
#
# Component   : INDI Server
# Dependencies: pthreads
# Supported OS: Linux, BSD, MacOS, Cygwin
#
# ##################################################################################################
if(INDI_BUILD_SERVER)
    add_subdirectory(indiserver)
endif()

# ##################################################################################################
#
# Component   : INDI Client
# Dependencies: zlib, cfitsio
# Supported OS: Linux, BSD, MacOS, Windows, Cygwin
# N.B. Windows support pending migration of networking code
#
# ##################################################################################################
if(INDI_BUILD_CLIENT AND NOT ANDROID)
    message(STATUS "Building INDI Client")
    add_subdirectory(libs/sockets)
    add_subdirectory(libs/indiclient)
endif(INDI_BUILD_CLIENT AND NOT ANDROID)

# ##################################################################################################
#
# Component   : INDI Qt5 Client
# Dependencies: Qt5Network, zlib, cfitsio, Qt5Core
# Supported OS: Linux, BSD, MacOS, Cygwin, Windows, Android
#
# ##################################################################################################
if(INDI_BUILD_QT5_CLIENT)
    message(STATUS "Building INDI Client with Qt5 support")
    add_subdirectory(libs/indiclientqt)
endif(INDI_BUILD_QT5_CLIENT)

# ##################################################################################################
#
# Component   : Websocket
# Dependencies: libwebsocketpp, libboost, asio
# Supported OS: Linux, BSD, MacOS, Cygwin
#
# ##################################################################################################
if(INDI_BUILD_WEBSOCKET)
    find_package(websocketpp REQUIRED)
    find_package(Boost COMPONENTS system thread)
    include_directories(${Boost_INCLUDE_DIRS})
    add_definitions(-DHAVE_WEBSOCKET)
    set(HAVE_WEBSOCKET 1)
endif(INDI_BUILD_WEBSOCKET)

# ##################################################################################################
#
# Component   : INDI Drivers, Tools, and Examples
# Dependencies: pthreads, usb1, zLib, cfitsio, nova, curl, jpeg (Linux Only)
# Supported OS: Linux, BSD, MacOS, Cygwin
# N.B. Webcam drivers only supported under Linux (Video4Linux2). Joystick support only under Linux
#
# ##################################################################################################
if(INDI_BUILD_DRIVERS)
    if(WIN32 OR ANDROID)
        message(WARNING "INDI drivers are only supported under Linux, BSD, MacOS, and Cygwin while current system is " ${CMAKE_SYSTEM_NAME})
    else(WIN32 OR ANDROID)
        # 1. Dependencies
        find_package(Threads REQUIRED)
        find_package(ZLIB REQUIRED)
        find_package(CFITSIO REQUIRED)
        find_package(Nova REQUIRED)
        find_package(USB1 REQUIRED)
        find_package(CURL REQUIRED)
        find_package(GSL REQUIRED)
        find_package(JPEG REQUIRED)
        find_library(M_LIB m)

        if(CMAKE_VERSION VERSION_LESS 3.12.0)
            set(CURL ${CURL_LIBRARIES})
        else()
            set(CURL CURL::libcurl)
        endif()

        include_directories(${CFITSIO_INCLUDE_DIR})
        include_directories(${NOVA_INCLUDE_DIR})
        include_directories(${USB1_INCLUDE_DIRS})
        include_directories(${GSL_INCLUDE_DIRS})
        include_directories(${JPEG_INCLUDE_DIR})
        include_directories(libs/indibase)
        include_directories(libs/indibase/timer)

        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-usb.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-usb.h)

        add_subdirectory(libs/eventloop)
        add_subdirectory(libs/dsp)
        add_subdirectory(libs/fpack)
        add_subdirectory(libs/hid)

        # #################################################
        # ########## INDI Driver Library ##################
        # #################################################
        add_subdirectory(libs/indibase)

        # #################################################
        # ########## INDI Alignment Subsystem #############
        # #################################################
        add_subdirectory(libs/alignment)

        # #################################################
        # ########## INDI Drivers #########################
        # #################################################
        add_subdirectory(drivers)
        install(FILES drivers.xml ${CMAKE_CURRENT_SOURCE_DIR}/drivers/focuser/indi_tcfs_sk.xml DESTINATION ${DATA_INSTALL_DIR})

        # ####################################
        # ########### INDI TOOLS #############
        # ####################################
        add_subdirectory(tools)

        # ####################################
        # ########### EXamples ###############
        # ####################################
        if(INDI_BUILD_CLIENT)
            add_subdirectory(examples)
        else()
            message(WARNING "Skipping build of examples since INDI POSIX client is not built")
        endif()

        # ################################################################################
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libindi.pc.cmake ${CMAKE_CURRENT_BINARY_DIR}/libindi.pc @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libindi.pc DESTINATION ${PKGCONFIG_INSTALL_PREFIX})

        # ##################################################################################################
        # ########################################  Tests  #################################################
        # ##################################################################################################
        if(DEFINED GTEST_ROOT)
            message(STATUS "Using GTEST from ${GTEST_ROOT}")
            add_subdirectory(${GTEST_ROOT}
                "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
            set(GTEST_FOUND true)
        else(DEFINED GTEST_ROOT)
            find_package(GTest)
        endif()

        find_package(GMock)

        if(GTEST_FOUND)
            if(INDI_BUILD_UNITTESTS)
                message(STATUS "Building unit tests")
                add_subdirectory(test)
            else(INDI_BUILD_UNITTESTS)
                message(STATUS "Not building unit tests")
            endif(INDI_BUILD_UNITTESTS)

            if(INDI_BUILD_INTEGTESTS)
                message(STATUS "Building integration tests")
                add_subdirectory(integs)
            else(INDI_BUILD_INTEGTESTS)
                message(STATUS "Not building integration tests")
            endif(INDI_BUILD_INTEGTESTS)

        else()
            message(STATUS "GTEST not found, not building tests")
        endif(GTEST_FOUND)
    endif(WIN32 OR ANDROID)
endif(INDI_BUILD_DRIVERS)

# ##################################################################################################
# ######################################  config.h  ################################################
# ##################################################################################################
# Generate config.h from template
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/indiversion.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/indiversion.h)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

# Install common dev files for all except server
if(INDI_BUILD_DRIVERS OR INDI_BUILD_CLIENT OR INDI_BUILD_QT5_CLIENT)
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/json.h
        ${CMAKE_CURRENT_BINARY_DIR}/indiversion.h
        DESTINATION ${INCLUDE_INSTALL_DIR}/libindi COMPONENT Devel
    )
endif(INDI_BUILD_DRIVERS OR INDI_BUILD_CLIENT OR INDI_BUILD_QT5_CLIENT)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

message(STATUS "The following components are going to be built:")

if(INDI_BUILD_SERVER)
    message(STATUS "## INDI Server")
endif()

if(INDI_BUILD_DRIVERS)
    message(STATUS "## INDI Drivers, Tools, and Examples")
endif()

if(INDI_BUILD_CLIENT)
    message(STATUS "## INDI Client")
endif()

if(INDI_BUILD_QT5_CLIENT)
    message(STATUS "## INDI Qt5 Client")
endif()
