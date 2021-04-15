#pragma once

#include <g6/utils/cpo.hpp>

namespace g6::web {
    constexpr struct make_server_cpo_ {
        template<typename Context, typename Proto, typename... Args>
        auto operator()(Context &ctx, const Proto &proto, Args &&...args) const
            noexcept(unifex::is_nothrow_tag_invocable_v<make_server_cpo_, Context &, const Proto &, Args...>) {
            return unifex::tag_invoke(*this, ctx, proto, std::forward<Args>(args)...);
        }
    } make_server{};

    constexpr struct async_serve_cpo_ {
        template<typename Server, typename... Args>
        auto operator()(Server &&server, Args &&...args) const
            noexcept(unifex::is_nothrow_tag_invocable_v<async_serve_cpo_, Server, Args...>)
                -> unifex::tag_invoke_result_t<async_serve_cpo_, Server, Args...> {
            return unifex::tag_invoke(*this, std::forward<Server>(server), std::forward<Args>(args)...);
        }
    } async_serve{};

    constexpr struct make_session_cpo_ {
        template<typename Server, typename Socket, typename Endpoint>
        auto operator()(Server const &server, Socket &&socket, Endpoint &&endpoint) const
            noexcept(unifex::is_nothrow_tag_invocable_v<make_session_cpo_, Server const &, Socket &&, Endpoint &&>) {
            return unifex::tag_invoke(*this, server, std::forward<Socket>(socket), std::forward<Endpoint>(endpoint));
        }
    } make_session{};

    namespace upgrade_connection_ {
        constexpr struct fn_ {
            // server connection upgrade
            template<typename Server, typename Connection>
            auto operator()(Server& server, Connection& connection) const {
                return unifex::tag_invoke(*this, server, connection);
            }

            // client connection upgrade
            template<typename BaseClient, typename FinalClient>
            auto operator()(BaseClient& base_client, std::type_identity<FinalClient> final_client_id) const {
                return unifex::tag_invoke(*this, base_client, final_client_id);
            }
        } upgrade_connection;
    }
    using upgrade_connection_::upgrade_connection;

    G6_CPO_DEF(get_context)
    G6_CPO_DEF(get_socket)

}// namespace g6::web
