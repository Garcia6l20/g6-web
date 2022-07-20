#pragma once

#include <g6/web/proto.hpp>

#include <g6/http/client.hpp>

#include <g6/ws/connection.hpp>

#include <g6/crypto/base64.hpp>

#include <random>

namespace g6 {

    namespace ws {

        template<typename Context, typename Socket>
        class client : public connection<false, Socket> {
        public:
            client(Context &context, Socket &&socket, net::ip_endpoint const &remote_endpoint) noexcept
                : connection<false, Socket>{std::forward<Socket>(socket), remote_endpoint} {}

            static std::string random_string(size_t len) {
                constexpr char charset[] = "0123456789"
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "abcdefghijklmnopqrstuvwxyz";

                static std::mt19937 rg{std::random_device{}()};
                static std::uniform_int_distribution<std::string::size_type> dist(0, sizeof(charset) - 2);

                std::string str{};
                str.resize(len);
                std::generate_n(str.begin(), len, [] { return char(dist(rg)); });
                return str;
            }

            //            template<typename Context_, typename Socket_>
            //            friend task<ws::client<Context_, Socket_>>
            //            g6::http::tag_invoke(tag_t<web::upgrade_connection>, std::type_identity<ws::client<Context_, Socket_>>,
            //                                 http::client<Context_, Socket> &http_client, Context_ &context, Socket_ &socket);

            template<typename Context2>
            friend auto tag_invoke(tag_t<net::async_connect>, Context2 &, web::proto::ws_ const &,
                                   const net::ip_endpoint &);
        };
    }// namespace ws

    namespace http {

        template<typename Context, typename Socket>
        task<ws::client<Context, Socket>>
        tag_invoke(tag_t<web::upgrade_connection>, http::client<Context, Socket> &http_client,
                   std::type_identity<ws::client<Context, Socket>>, std::string_view path = "/", ws::sub_protocols protos = {}) {
            const auto key_base = ws::client<Context, Socket>::random_string(16);
            std::string hash = crypto::base64::encode(key_base);
            spdlog::debug("ws::upgrade: key: {}, base64: {}", key_base, hash);
            http::headers hdrs{
                {"Connection", "Upgrade"},
                {"Upgrade", "websocket"},
                {"Sec-WebSocket-Key", hash},
                {"Sec-WebSocket-Version", std::to_string(ws::client<Context, Socket>::max_ws_version_)},
            };
            if (not protos.empty()) {
                for (auto const &p : protos) {
                    hdrs.insert({"Sec-WebSocket-Protocol", std::string{p}});
                }
            }
            auto response =
                co_await net::async_send(http_client, std::string_view{""}, path, http::method::get, std::move(hdrs));
            std::string body;
            co_await net::async_recv(response, std::back_inserter(body));
            if (response.status_code() != http::status::switching_protocols) {
                throw std::system_error(int(response.status_code()), http::error_category, "upgrade_connection");
            }
            co_return ws::client{web::get_context(http_client), std::move(web::get_socket(http_client)),
                                 http_client.remote_endpoint()};
        }
    }// namespace http

    namespace net {
        template<typename Context>
        task<ws::client<Context, net::async_socket>>
        tag_invoke(tag_t<net::async_connect>, Context &context, g6::web::proto::ws_ const &,
                   const net::ip_endpoint &endpoint, std::string_view path = "/", ws::sub_protocols protos = {}) {
            auto http_client = co_await net::async_connect(context, web::proto::http, endpoint);
            co_return co_await web::upgrade_connection(
                http_client, std::type_identity<g6::ws::client<Context, net::async_socket>>{}, path, std::move(protos));
        }

        template<typename Context>
        task<ws::client<Context, ssl::async_socket>>
        tag_invoke(tag_t<net::async_connect>, Context &context, g6::web::proto::wss_ const &,
                   const net::ip_endpoint &endpoint, std::string_view path = "/",
                   ssl::verify_flags verify_flags = ssl::verify_flags::none, ws::sub_protocols protos = {}) {
            auto http_client = co_await net::async_connect(context, web::proto::https, endpoint, verify_flags);
            co_return co_await web::upgrade_connection(
                http_client, std::type_identity<g6::ws::client<Context, ssl::async_socket>>{}, path, std::move(protos));
        }
    }// namespace net
}// namespace g6
