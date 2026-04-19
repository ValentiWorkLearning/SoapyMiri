########################################################################
# Build Soapy SDR support module for libmirisdr
########################################################################
cmake_minimum_required(VERSION 3.8)

project(SoapyMiri C CXX)

find_package(SoapySDR "0.4.0" NO_MODULE REQUIRED)
if (NOT SoapySDR_FOUND)
    message(FATAL_ERROR "Soapy SDR development files not found...")
endif ()

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(libusb REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    libmirisdr
    GIT_REPOSITORY https://github.com/ValentiWorkLearning/libmirisdr-6.git
    GIT_TAG master
)


FetchContent_MakeAvailable(libmirisdr)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

SOAPY_SDR_MODULE_UTIL(
    TARGET soapyMiriSupport
    SOURCES
        SoapyMiri.hpp
        Registration.cpp
        Settings.cpp
        Streaming.cpp
    LIBRARIES
        mirisdr::mirisdr_static
        PkgConfig::libusb
)

# Rename the output to avoid conflicts with system installation
set_target_properties(soapyMiriSupport PROPERTIES
    OUTPUT_NAME "soapyMiriSupport-dev"
    PREFIX ""
)
