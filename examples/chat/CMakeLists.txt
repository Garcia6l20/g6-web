find_program(XXD_EXE xxd)
if(NOT XXD_EXE)
  message(WARNING "Cannot find xxd, chat example disabled")
  return()
endif()

add_custom_command(OUTPUT js_main.hpp
  COMMAND ${XXD_EXE} -i main.js ${CMAKE_CURRENT_BINARY_DIR}/js_main.hpp
  DEPENDS main.js
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  )

add_custom_command(OUTPUT html_index.hpp
  COMMAND ${XXD_EXE} -i index.html ${CMAKE_CURRENT_BINARY_DIR}/html_index.hpp
  DEPENDS index.html
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  )

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(spdlog REQUIRED)

add_executable(g6-chat
  main.cpp
  js_main.hpp
  html_index.hpp)
target_link_libraries(g6-chat PRIVATE spdlog::spdlog g6::web g6::router)

add_executable(g6-chat-bot
  bot.cpp)
target_link_libraries(g6-chat-bot PRIVATE spdlog::spdlog g6::web)
