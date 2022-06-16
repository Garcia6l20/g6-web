#pragma once

#include <g6/http/http.hpp>
#include <g6/http/session.hpp>

#include <g6/net/async_socket.hpp>
#include <g6/net/ip_endpoint.hpp>
#include <g6/ssl/async_socket.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

#include <spdlog/spdlog.h>

namespace g6 {
    namespace web {
        class context;
    }
    namespace http {        

        template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
        task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &, std::stop_token,
                              RequestHandlerBuilder &&);

        template<typename Socket>
        class server {
            g6::web::context &context_;
        public:
            server(g6::web::context &context, Socket socket) : context_{context}, socket{std::move(socket)} {}

        public:
            static constexpr auto proto = web::proto::http;

            Socket socket;

            using connection_type = server_session<Socket>;

            server() = delete;
            server(server const &) = delete;
            server(server &&) noexcept = default;
            ~server() = default;

            template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
            friend task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &, std::stop_token,
                                         RequestHandlerBuilder &&);
        };

        template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
        task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &server, std::stop_token stop_token,
                              RequestHandlerBuilder &&request_handler_builder) {
            ff_spawner scope;
            try {
                while (not stop_token.stop_requested()) {
                    auto [sock, address] = co_await net::async_accept(server.socket, stop_token);
                    static_assert(std::same_as<std::decay_t<decltype(sock)>, std::decay_t<decltype(server.socket)>>);
                    auto http_session = server_session<SocketT>{std::move(sock), address};
                    spdlog::info("client connected: {}", address);
                    auto session = co_await web::upgrade_connection(server.proto, http_session);
                    auto handler = request_handler_builder();

                    if constexpr (requires{ co_await handler(std::move(session), stop_token); }) {
                        // session handler
                        scope.spawn(handler(std::move(session), stop_token));
                    } else {
                        // assume request handler
                        scope.spawn([](auto session, auto request_handler, std::stop_token stop) mutable -> task<void> {
                            try {
                                while (not stop.stop_requested()) {
                                    co_await request_handler(session, net::async_recv(session));
                                }
                            } catch (std::system_error const &error) {
                                auto ec = error.code();
                                if (ec == std::errc::connection_reset) {
                                    spdlog::info("connection reset '{}'", session.remote_endpoint());
                                } else if (ec == std::errc::operation_canceled) {
                                    spdlog::info("operation canceled '{}'", session.remote_endpoint());
                                } else {
                                    spdlog::error("connection {} error '{}'", session.remote_endpoint(),
                                                error.code().message());
                                    throw;
                                }
                            } catch (std::exception const &error) {
                                spdlog::error("connection {} error '{}'", session.remote_endpoint(), error.what());
                                throw;
                            }
                        }(std::move(session), std::move(handler), stop_token));
                    }
                }
            } catch (std::system_error const &error) {
                if (error.code() == std::errc::operation_canceled) {
                    spdlog::info("server stopped");
                } else {
                    spdlog::error("server error '{}'", error.code().message());
                    throw;
                }
            }
            co_await scope;
        }

        template<typename Socket_>
        inline task<http::server_session<Socket_>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::http_,
                                                              http::server_session<Socket_> &session) {
            co_return std::move(session);
        }

    }// namespace http

    namespace web {

        inline http::server<net::async_socket> tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx, web::proto::http_ const &,
                            net::ip_endpoint endpoint) {
            auto socket = net::open_socket(ctx, net::proto::tcp);
            socket.bind(endpoint);
            socket.listen();
            return http::server{ctx, std::move(socket)};
        }

        inline http::server<ssl::async_socket> tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx, web::proto::https_ const &,
                            net::ip_endpoint endpoint, const ssl::certificate &cert, const ssl::private_key &key) {
            auto socket = net::open_socket(ctx, std::move(endpoint), cert, key);
            return http::server{ctx, std::move(socket)};
        }
    }

}// namespace g6
