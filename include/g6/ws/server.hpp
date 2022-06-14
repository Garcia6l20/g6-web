#pragma once

#include <g6/crypto/base64.hpp>
#include <g6/crypto/sha1.hpp>
#include <g6/http/server.hpp>

#include <g6/ws/connection.hpp>
#include <stop_token>

namespace g6 {

    namespace ws {
        template<typename Socket>
        class server_session : public connection<true, Socket> {
        public:
            explicit server_session(Socket &&socket, net::ip_endpoint const &endpoint,
                                    uint32_t version = connection<true, Socket>::max_ws_version_) noexcept
                : connection<true, Socket>{std::forward<Socket>(socket), endpoint, version} {}
        };

        template<typename Socket>
        class server : public g6::http::server<Socket> {
        public:
            static constexpr auto proto = web::proto::ws;
            server(g6::web::context &context, Socket socket) noexcept
                : g6::http::server<Socket>{context, std::move(socket)} {}
        };
    }// namespace ws

    namespace http {

        template<typename Socket>
        task<ws::server_session<Socket>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::ws_,
                                                    http::server_session<Socket> &http_session) {
            auto req = net::async_recv(http_session);
            co_return co_await web::upgrade_connection(web::proto::ws, http_session, req);
        }

        template<typename Socket>
        task<ws::server_session<Socket>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::ws_,
                                                    http::server_session<Socket> &http_session,
                                                    http::server_request<Socket> &request) {
            std::string body;
            co_await net::async_recv(request, std::back_inserter(body));
#ifdef G6_WEB_DEBUG
            for (auto &h : request.headers()) { spdlog::debug("{} -> {}", h.first, h.second); }
#endif
            auto con_header = request.header("Connection");
            auto upgrade_header = request.header("Upgrade");
            bool upgrade = con_header.find("Upgrade") != std::string::npos;
            bool websocket = upgrade_header == "websocket";

            if (not upgrade or not websocket) {
                spdlog::warn("got a non-websocket connection");
                co_await net::async_send(http_session, http::status::bad_request);
                throw std::system_error{std::make_error_code(std::errc::connection_reset)};
            }

            uint32_t ws_version = ws::server_session<Socket>::max_ws_version_;
            if (auto const &version = request.header("Sec-WebSocket-Version"); not version.empty()) {
                std::from_chars(version.data(), version.data() + version.size(), ws_version);
            }
            auto &accept = request.header("Sec-WebSocket-Key");
            accept = crypto::base64::encode(crypto::sha1::hash(accept, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
#ifdef G6_WEB_DEBUG
            spdlog::debug("accept-hash: {}", accept);
#endif
            http::headers hdrs{{"Upgrade", "websocket"},
                               {"Connection", "Upgrade"},
                               {"Sec-WebSocket-Accept", std::move(accept)}};
            co_await net::async_send(http_session, http::status::switching_protocols, std::move(hdrs));
            co_return ws::server_session<Socket>{std::move(web::get_socket(http_session)),
                                                 http_session.remote_endpoint(), ws_version};
        }
    }// namespace http

    namespace web {
        inline ws::server<net::async_socket> tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx,
                                                        web::proto::ws_ const &, net::ip_endpoint endpoint) {
            auto socket = net::open_socket(ctx, net::proto::tcp);
            socket.bind(endpoint);
            socket.listen();
            return {ctx, std::move(socket)};
        }

        inline ws::server<ssl::async_socket> tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx,
                                                        web::proto::wss_ const &, net::ip_endpoint endpoint,
                                                        const ssl::certificate &cert, const ssl::private_key &key) {
            auto socket = net::open_socket(ctx, std::move(endpoint), cert, key);
            return {ctx, std::move(socket)};
        }
    }// namespace web

}// namespace g6
