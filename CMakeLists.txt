cmake_minimum_required(VERSION 2.8)
project(APAPI)

#set(CMAKE_C_COMPILER clang)
#plugin source files
set(PLUGIN_SOURCE apapi.c)


option(VT_INC "Path of the vampirtrace headers" "")
option(PAPI_INC "Path of the PAPI headers" "")

OPTION(BACKEND_SCOREP "Build plugin using scorep(ON) or vampirtrace(OFF)" ON)

#additional c flags
set(CMAKE_C_FLAGS "-D_GNU_SOURCE -std=c11 -O2 -g")

if(BACKEND_SCOREP)
    include(common/FindScorep.cmake)
    if(SCOREP_FOUND)
        include_directories(${SCOREP_INCLUDE_DIRS})
    else()
        message(SEND_ERROR "Scorep was not found but is required!")
    endif()
    add_definitions("-DBACKEND_SCOREP")
else()
    include(common/FindVampirTrace.cmake)
    if(VT_FOUND)
        include_directories(${VT_INCLUDE_DIRS})
    else()
        message(SEND_ERROR "Vampirtrace was not found but is required!")
    endif()
    add_definitions("-DBACKEND_VTRACE")
endif()


#debugging c flags
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DHAVE_DEBUG -O0 -Wstrict-prototypes -Wall -Wundef -Wno-long-long -Wsign-compare -Wcomment -pedantic -finline-functions -fno-strict-aliasing")
set(CMAKE_C_FLAGS_RELEASE "-Os")

#Find PAPI inlcude directory
unset(__PAPI_INC CACHE)
find_path(__PAPI_INC NAMES "papi.h" HINTS ${PAPI_INC} $ENV{PAPI_INC})
if(NOT IS_ABSOLUTE ${__PAPI_INC})
    message(FATAL_ERROR "Could not find PAPI headers. Specify the path with -DPAPI_INC=<path to PAPI>.")
endif(NOT IS_ABSOLUTE ${__PAPI_INC})
message(STATUS "Found PAPI headers in ${__PAPI_INC}.")

include_directories( ${__PAPI_INC})
add_library(${PROJECT_NAME} SHARED ${PLUGIN_SOURCE})
target_link_libraries(${PROJECT_NAME}  pthread m)
