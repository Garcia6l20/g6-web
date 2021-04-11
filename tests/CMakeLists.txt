find_package(spdlog REQUIRED)

link_libraries(spdlog::spdlog g6::web)

add_subdirectory(http)
