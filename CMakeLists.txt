cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)

if (NOT G6_CMAKE)
  include(FetchContent)
  FetchContent_declare(g6-cmake-fetch
    GIT_REPOSITORY https://github.com/Garcia6L20/g6-cmake.git
    GIT_TAG main)
  FetchContent_makeAvailable(g6-cmake-fetch)
endif()

find_package(ctre REQUIRED)
find_package(spdlog REQUIRED)

if(NOT TARGET g6::router)
  FetchContent_declare(g6-router-fetch
    GIT_REPOSITORY https://github.com/Garcia6L20/g6-router.git
    GIT_TAG main)
  FetchContent_makeAvailable(g6-router-fetch)
endif()

if(NOT TARGET g6::net)
  FetchContent_declare(g6-net-fetch
    GIT_REPOSITORY https://github.com/Garcia6L20/g6-net.git
    GIT_TAG main)
  FetchContent_makeAvailable(g6-net-fetch)
endif()

#
# nodejs/http_parser
#
set(http_parser_DIR ${CMAKE_CURRENT_LIST_DIR}/external/http-parser)

project(g6-web
  LANGUAGES CXX C
  VERSION 0.0.1
  )

file(GLOB_RECURSE ${PROJECT_NAME}_headers include/)

add_library(${PROJECT_NAME} ${${PROJECT_NAME}_headers}
  src/uri.cpp
  src/static_parser_handler.cpp
  ${http_parser_DIR}/http_parser.c
  ${http_parser_DIR}/http_parser.h
  )
add_library(g6::web ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  )

target_include_directories(${PROJECT_NAME} PRIVATE
  ${http_parser_DIR}
  )

target_link_libraries(${PROJECT_NAME} PUBLIC
  g6::tl
  g6::net
  g6::router
  ctre::ctre)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog)

option(G6_WEB_DEBUG "Enable debug logs in g6::web" OFF)
if(G6_WEB_DEBUG)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DG6_WEB_DEBUG=1)
endif()

enable_testing()
if (BUILD_TESTING)
  add_subdirectory(tests)
endif()

option(G6_HTTP_BUILD_EXAMPLES "Build G6 HTTP examples" ON)
if(G6_BUILD_EXAMPLES OR G6_HTTP_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(DIRECTORY include/ DESTINATION include
  FILES_MATCHING PATTERN *)
