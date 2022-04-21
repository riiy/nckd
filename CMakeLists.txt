cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
set(SUPPORTED_CXX_STANDARDS 11 14 17 20) 

# Set required C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE)
   set (CMAKE_BUILD_TYPE "Debug")
   message (
      STATUS "No CMAKE_BUILD_TYPE selected, defaulting to ${CMAKE_BUILD_TYPE}"
   )
endif ()

## Project 
set(NCKD_PROJECT_NAME
    Nckd
)
set(NCKD_SOURCE_DIR
    src
)
set(NCKD_SOURCE
    ${NCKD_SOURCE_DIR}/main.cpp
)
set(NCKD_LIBRARIES_DIR
    libs
)
set(NCKD_LIBRARIES
    httplib
    cpool
    spdlog
    jwt-cpp
)
# Third Lib
## cpp-httplib
set(HTTPLIB_COMPILE ON)

## Test
# include(GoogleTest)
enable_testing()
add_subdirectory(libs/gtest)
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

## Json
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(libs/json)

## JWT
set(JWT_DISABLE_PICOJSON ON)
set(JWT_BUILD_EXAMPLES OFF)


###########
# Project #
###########

project(${NCKD_PROJECT_NAME}
  LANGUAGES CXX)
## Boost
find_package(Boost COMPONENTS program_options REQUIRED)

if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost Not found")
endif()
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

## Threads
find_package(Threads REQUIRED)

## PostgreSQL
find_package(PostgreSQL REQUIRED)

add_executable(${NCKD_PROJECT_NAME} ${NCKD_SOURCE})
foreach(LIBRARY ${NCKD_LIBRARIES})
    add_subdirectory("${NCKD_LIBRARIES_DIR}/${LIBRARY}")
endforeach(LIBRARY)
target_link_libraries(${NCKD_PROJECT_NAME} ${NCKD_LIBRARIES} nlohmann_json::nlohmann_json Boost::program_options PostgreSQL::PostgreSQL argon2)
add_subdirectory(tests)
