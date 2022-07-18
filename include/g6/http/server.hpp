#pragma once

#include <g6/http/http.hpp>
#include <g6/http/session.hpp>

#include <g6/net/async_socket.hpp>
#include <g6/net/ip_endpoint.hpp>
#include <g6/ssl/async_socket.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

#include <g6/coro/spawn.hpp>

#include <spdlog/spdlog.h>

namespace g6 {
    namespace web {
        class context;
    }
    namespace http {

        template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
        task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &, RequestHandlerBuilder &&);

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
            friend task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &, RequestHandlerBuilder &&);
        };

        template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
        task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &server,
                              RequestHandlerBuilder &&request_handler_builder) {
            try {
                while (true) {
                    auto [sock, address] = co_await net::async_accept(server.socket);
                    static_assert(std::same_as<std::decay_t<decltype(sock)>, std::decay_t<decltype(server.socket)>>);
                    auto http_session = server_session<SocketT>{std::move(sock), address};
                    spdlog::info("client connected: {}", address);
                    auto session = co_await web::upgrade_connection(server.proto, http_session);
                    auto handler = request_handler_builder();

                    if constexpr (requires { co_await handler(std::move(session)); }) {
                        // session handler
                        spawn(handler(std::move(session)));
                    } else {
                        // assume request handler
                        spawn([](auto session, auto request_handler) mutable -> task<void> {
                            try {
                                while (true) { co_await request_handler(session, net::async_recv(session)); }
                            } catch (operation_cancelled const &) {
                                spdlog::info("operation canceled '{}'", session.remote_endpoint());
                            } catch (std::system_error const &error) {
                                auto ec = error.code();
                                if (ec == std::errc::connection_reset) {
                                    spdlog::info("connection reset '{}'", session.remote_endpoint());
                                } else {
                                    spdlog::error("connection {} error '{}'", session.remote_endpoint(),
                                                  error.code().message());
                                    throw;
                                }
                            } catch (std::exception const &error) {
                                spdlog::error("connection {} error '{}'", session.remote_endpoint(), error.what());
                                throw;
                            }
                        }(std::move(session), std::move(handler)));
                    }
                }
            } catch (operation_cancelled const &) {
                spdlog::info("server stopped");
            } catch (std::system_error const &error) {
                spdlog::error("server error '{}'", error.code().message());
                throw;
            }
        }

        template<typename Socket_>
        inline task<http::server_session<Socket_>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::http_,
                                                              http::server_session<Socket_> &session) {
            co_return std::move(session);
        }

    }// namespace http

    namespace web {

        inline http::server<net::async_socket> tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx,
                                                          web::proto::http_ const &, net::ip_endpoint endpoint) {
            auto socket = net::open_socket(ctx, net::proto::tcp);
            socket.bind(endpoint);
            socket.listen();
            return http::server{ctx, std::move(socket)};
        }

        inline http::server<ssl::async_socket> tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx,
                                                          web::proto::https_ const &, net::ip_endpoint endpoint,
                                                          const ssl::certificate &cert, const ssl::private_key &key) {
            auto socket = net::open_socket(ctx, std::move(endpoint), cert, key);
            return http::server{ctx, std::move(socket)};
        }
    }// namespace web

}// namespace g6
