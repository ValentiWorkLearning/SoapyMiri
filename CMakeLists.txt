########################################################################
# Build Soapy SDR support module for libmirisdr
########################################################################
cmake_minimum_required(VERSION 3.10...3.27)

project(SoapyMiri C CXX)

include(FetchContent)

option(USE_SYSTEM_SOAPYSDR "Use system-installed SoapySDR if available" ON)

if(USE_SYSTEM_SOAPYSDR)
    find_package(SoapySDR CONFIG QUIET)
endif()



if(NOT SoapySDR_FOUND)
    message(STATUS "SoapySDR not found, fetching with FetchContent")

    FetchContent_Declare(
        SoapySDR
        GIT_REPOSITORY https://github.com/pothosware/SoapySDR.git
        GIT_TAG        soapy-sdr-0.8.1
    )

    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_PYTHON OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(SoapySDR)
else()
    message(STATUS "Using system SoapySDR")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#enable c++11 features
if (CMAKE_COMPILER_IS_GNUCXX)

    #C++11 is a required language feature for this project
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++11" HAS_STD_CXX11)
    if (HAS_STD_CXX11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    else (HAS_STD_CXX11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    endif ()

    #Thread support enabled (not the same as -lpthread)
    list(APPEND LIBMIRISDR_LIBRARIES -pthread)

    #disable warnings for unused parameters
    add_definitions(-Wno-unused-parameter)

endif (CMAKE_COMPILER_IS_GNUCXX)

if (APPLE)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wc++11-extensions")
endif (APPLE)

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(libusb QUIET IMPORTED_TARGET libusb-1.0)
endif()

if(NOT TARGET PkgConfig::libusb)
    message(STATUS "libusb-1.0 not found via pkg-config, fetching libusb...")

    FetchContent_Declare(
        libusb
        GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
        GIT_TAG        v1.0.27
    )

    FetchContent_MakeAvailable(libusb)
    if(TARGET usb-1.0)
     message(STATUS "Found libusb target: usb-1.0")
    elseif(TARGET libusb-1.0)
        message(STATUS "Found libusb target: libusb-1.0")
    elseif(TARGET libusb)
        message(STATUS "Found libusb target: libusb")
    else()
        message(FATAL_ERROR "No known libusb target found")
    endif()

    set(_libusb_include_dir "${libusb_SOURCE_DIR}/libusb/libusb")

    if(NOT EXISTS "${_libusb_include_dir}/libusb.h")
        message(FATAL_ERROR "libusb.h not found at: ${_libusb_include_dir}")
    endif()

    set(LIBUSB_FOUND TRUE CACHE BOOL "" FORCE)

    set(LIBUSB_INCLUDE_DIR  "${_libusb_include_dir}" CACHE PATH "" FORCE)
    set(LIBUSB_INCLUDE_DIRS "${_libusb_include_dir}" CACHE STRING "" FORCE)
    set(LIBUSB_LIBRARIES    "${_libusb_target}" CACHE STRING "" FORCE)

endif()

set(BUILD_TEST_APPS OFF CACHE BOOL "" FORCE)
set(LIBMIRISDR_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
include(FetchContent)

FetchContent_Declare(
    libmirisdr
    GIT_REPOSITORY https://github.com/ValentiWorkLearning/libmirisdr-6.git
    GIT_TAG vkorniienko/improve/windows-build-fix
)

FetchContent_GetProperties(libmirisdr)

if(NOT libmirisdr_POPULATED)
    FetchContent_Populate(libmirisdr)

    add_subdirectory(
        "${libmirisdr_SOURCE_DIR}"
        "${libmirisdr_BINARY_DIR}"
        EXCLUDE_FROM_ALL
    )
endif()

FetchContent_Declare(
    moodycamel
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG master
)

FetchContent_MakeAvailable(moodycamel)

FetchContent_Declare(
    moodycamel_readerwriterqueue
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
    GIT_TAG master
)

FetchContent_MakeAvailable(moodycamel_readerwriterqueue)


add_library(moodycamel INTERFACE)
target_include_directories(moodycamel INTERFACE
    ${moodycamel_SOURCE_DIR}
)

add_library(moodycamel_readerwriterqueue INTERFACE)
target_include_directories(moodycamel_readerwriterqueue INTERFACE
    ${moodycamel_readerwriterqueue_SOURCE_DIR}
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})


if(TARGET PkgConfig::libusb)
    set(LIBUSB_TARGET_LIBRARY_PACKAGE  PkgConfig::libusb)
elseif(TARGET usb-1.0)
    set(LIBUSB_TARGET_LIBRARY_PACKAGE usb-1.0)
else()
    message(FATAL_ERROR "libusb target was not found or created")
endif()

SOAPY_SDR_MODULE_UTIL(
    TARGET soapyMiriSupport
    SOURCES
        SoapyMiri.hpp
        Registration.cpp
        Settings.cpp
        Streaming.cpp
    LIBRARIES
        mirisdr::mirisdr_static
        ${LIBUSB_TARGET_LIBRARY_PACKAGE}
        moodycamel
        moodycamel_readerwriterqueue
)

# Rename the output to avoid conflicts with system installation
set_target_properties(soapyMiriSupport PROPERTIES
    OUTPUT_NAME "soapyMiriSupport-dev"
    PREFIX ""
)
