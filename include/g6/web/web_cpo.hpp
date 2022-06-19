#pragma once

#include <g6/tag_invoke>

namespace g6::web {

    G6_MAKE_CPO(make_server)
    G6_MAKE_CPO(async_serve)

    G6_MAKE_CPO(make_session)
    G6_MAKE_CPO(upgrade_connection)

    G6_MAKE_CPO(get_context)
    G6_MAKE_CPO(get_socket)

}// namespace g6::web
