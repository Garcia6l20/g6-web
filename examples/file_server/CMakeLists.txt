find_package(spdlog REQUIRED)

add_executable(g6-fileserver
  main.cpp)
target_link_libraries(g6-fileserver PRIVATE g6::web g6::router g6-tmp spdlog::spdlog)

install(TARGETS g6-fileserver
  RUNTIME DESTINATION bin)
