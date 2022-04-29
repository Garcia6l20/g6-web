find_program(XXD_EXE xxd)
if(NOT XXD_EXE)
  message(WARNING "Cannot find xxd, chat example disabled")
  return()
endif()

function(make_resource input output)
  add_custom_command(OUTPUT ${output}
    COMMAND ${XXD_EXE} -i ${input} ${CMAKE_CURRENT_BINARY_DIR}/${output}
    DEPENDS ${input}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    )
endfunction()

make_resource(main.js main_js.hpp)
make_resource(index.html index_html.hpp)
make_resource(login.html login_html.hpp)


set(CMAKE_CXX_STANDARD 23)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(spdlog REQUIRED)

add_executable(g6-chat
  main.cpp
  main_js.hpp
  index_html.hpp
  login_html.hpp)
target_link_libraries(g6-chat PRIVATE spdlog::spdlog g6::web g6::router)

add_executable(g6-chat-bot
  bot.cpp)
target_link_libraries(g6-chat-bot PRIVATE spdlog::spdlog g6::web)
