#pragma once

#include <g6/crypto/base64.hpp>
#include <g6/crypto/sha1.hpp>
#include <g6/http/server.hpp>

#include <g6/ws/connection.hpp>

namespace g6 {

    namespace web {
        auto tag_invoke(tag_t<make_server>, g6::web::context &ctx, web::proto::ws_ const &, net::ip_endpoint endpoint);
    }

    namespace ws {
        template<typename Socket>
        class server_session : public connection<true, Socket>
        {
        public:
            explicit server_session(Socket &&socket, net::ip_endpoint const &endpoint,
                                    uint32_t version = connection<true, Socket>::max_ws_version_) noexcept
                : connection<true, Socket>{std::forward<Socket>(socket), endpoint, version} {}
        };

        template<typename Context, typename Socket>
        class server : public g6::http::server<Context, Socket>
        {
        public:
            static constexpr auto proto = web::proto::ws;

        private:
            server(Context &context, Socket socket) noexcept
                : g6::http::server<Context, Socket>{context, std::move(socket)} {}

            friend auto web::tag_invoke(tag_t<web::make_server>, g6::web::context &ctx, web::proto::ws_ const &,
                                        net::ip_endpoint endpoint);
        };
    }// namespace ws

    namespace http {

        template<typename Socket>
        task<ws::server_session<Socket>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::ws_,
                                                    http::server_session<Socket> &http_session) {
            auto request = co_await net::async_recv(http_session);
            co_return co_await web::upgrade_connection(web::proto::ws, http_session, request);
        }

        template<typename Socket>
        task<ws::server_session<Socket>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::ws_,
                                                    http::server_session<Socket> &http_session,
                                                    http::server_request<Socket> &request) {
            while (net::has_pending_data(request)) { co_await net::async_recv(request); }
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
            std::string_view dumb{};
            co_await net::async_send(http_session, http::status::switching_protocols, std::move(hdrs),
                                     as_bytes(std::span{dumb.data(), dumb.size()}));
            co_return ws::server_session<Socket>{std::move(web::get_socket(http_session)),
                                                 http_session.remote_endpoint(), ws_version};
        }
    }// namespace http

}// namespace g6
