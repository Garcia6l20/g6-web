add_executable(g6-fileserver
  main.cpp)
target_link_libraries(g6-fileserver PRIVATE g6::web g6::router g6-tmp)
