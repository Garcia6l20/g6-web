#pragma once

#include <g6/crypto/base64.hpp>
#include <g6/crypto/sha1.hpp>
#include <g6/http/server.hpp>

#include <g6/ws/connection.hpp>

namespace g6 {

    namespace web {
        template<typename Context>
        auto tag_invoke(tag_t<make_server>, Context &ctx, web::proto::ws_ const &, net::ip_endpoint endpoint);
    }

    namespace ws {
        template<typename Socket>
        class server_session : public connection<true, Socket>
        {
        public:
            explicit server_session(Socket &&socket,
                                    uint32_t version = connection<true, Socket>::max_ws_version_) noexcept
                : connection<true, Socket>{std::forward<Socket>(socket), version} {}
        };

        template<typename Context, typename Socket>
        class server : public g6::http::server<Context, Socket>
        {
        public:
            using connection_type = server_session<Socket>;

        private:
            server(Context &context, Socket socket) noexcept
                : g6::http::server<Context, Socket>{context, std::move(socket)} {}

            template<typename Context_>
            friend auto web::tag_invoke(tag_t<web::make_server>, Context_ &ctx, web::proto::ws_ const &,
                                        net::ip_endpoint endpoint);
        };
    }// namespace ws

    namespace http {

        template<typename Context, typename Socket>
        task<ws::server_session<Socket>> tag_invoke(tag_t<web::upgrade_connection>, ws::server<Context, Socket> &server,
                                                    http::server_session<Socket> &http_session) {
            auto req = co_await net::async_recv(http_session);
            while (net::has_pending_data(req)) { co_await net::async_recv(req); }
#ifdef G6_WEB_DEBUG
            for (auto &h : req.headers()) { spdlog::debug("{} -> {}", h.first, h.second); }
#endif
            auto con_header = req.header("Connection");
            auto upgrade_header = req.header("Upgrade");
            bool upgrade = con_header == "Upgrade";
            bool websocket = upgrade_header == "websocket";

            if (not upgrade or not websocket) {
                spdlog::warn("got a non-websocket connection");
                co_await net::async_send(http_session, http::status::bad_request);
                throw std::system_error{std::make_error_code(std::errc::connection_reset)};
            }

            uint32_t ws_version = ws::server_session<Socket>::max_ws_version_;
            if (auto const &version = req.header("Sec-WebSocket-Version"); not version.empty()) {
                std::from_chars(version.data(), version.data() + version.size(), ws_version);
            }
            auto &accept = req.header("Sec-WebSocket-Key");
            accept = crypto::base64::encode(crypto::sha1::hash(accept, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
#ifdef G6_WEB_DEBUG
            spdlog::debug("accept-hash: {}", accept);
#endif
            http::headers hdrs{{"Upgrade", "websocket"},
                               {"Connection", "Upgrade"},
                               {"Sec-WebSocket-Accept", std::move(accept)}};
            std::string_view dumb{};
            co_await net::async_send(http_session, http::status::switching_protocols, std::move(hdrs),
                                     as_bytes(span{dumb.data(), dumb.size()}));
            co_return ws::server_session<Socket>{std::move(web::get_socket(http_session)), ws_version};
        }
    }// namespace http

    namespace web {
        template<typename Context>
        auto tag_invoke(tag_t<make_server>, Context &ctx, web::proto::ws_ const &, net::ip_endpoint endpoint) {
            auto socket = net::open_socket(ctx, net::tcp_server, std::move(endpoint));
            return ws::server{ctx, std::move(socket)};
        }
    }// namespace web
}// namespace g6
