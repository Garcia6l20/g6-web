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

        auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::http_ const &, net::ip_endpoint);
        auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::https_ const &, net::ip_endpoint,
                        const auto &, const auto &);
    }// namespace web

    namespace http {

        template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
        task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &, std::stop_source &,
                              RequestHandlerBuilder &&);

        template<typename Socket>
        class server {
        protected:
            g6::web::context &context_;
            ff_spawner scope_{};
            server(g6::web::context &context, Socket socket) : context_{context}, socket{std::move(socket)} {}

        public:
            static constexpr auto proto = web::proto::http;

            Socket socket;

            using connection_type = server_session<Socket>;

            server() = delete;
            server(server const &) = delete;
            server(server &&) noexcept = default;
            ~server() = default;

            friend auto g6::web::tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::http_ const &,
                                            net::ip_endpoint);

            friend auto g6::web::tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::https_ const &,
                                            net::ip_endpoint, const auto &, const auto &);

            template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
            friend task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &, std::stop_source &,
                                         RequestHandlerBuilder &&);
        };

        template<template<typename> typename Server, typename SocketT, typename RequestHandlerBuilder>
        task<void> tag_invoke(tag_t<g6::web::async_serve>, Server<SocketT> &server, std::stop_source &stop_source,
                              RequestHandlerBuilder &&request_handler_builder) {

            auto &scope = server.scope_;
            try {
                while (not stop_source.stop_requested()) {
                    auto [sock, address] = co_await net::async_accept(server.socket, stop_source.get_token());
                    static_assert(std::same_as<std::decay_t<decltype(sock)>, std::decay_t<decltype(server.socket)>>);
                    auto http_session = server_session<SocketT>{std::move(sock), address};
                    spdlog::info("client connected: {}", address.to_string());
                    auto session = co_await web::upgrade_connection(server.proto, http_session);
                    scope.spawn(
                        [](auto session, auto request_handler, std::stop_token stop) mutable -> task<void> {
                            try {
                                while (not stop.stop_requested()) {
                                    auto request = co_await net::async_recv(session, stop);
                                    co_await request_handler(session, std::move(request));
                                }
                            } catch (std::system_error const &error) {
                                auto ec = error.code();
                                if (ec == std::errc::connection_reset) {
                                    spdlog::info("connection reset '{}'", session.remote_endpoint().to_string());
                                } else if (ec == std::errc::operation_canceled) {
                                    spdlog::info("operation canceled '{}'", session.remote_endpoint().to_string());
                                } else {
                                    spdlog::error("connection {} error '{}'", session.remote_endpoint().to_string(),
                                                  error.code().message());
                                    throw;
                                }
                            } catch (std::exception const &error) {
                                spdlog::error("connection {} error '{}'", session.remote_endpoint().to_string(),
                                              error.what());
                                throw;
                            }
                        }(std::move(session), std::forward<RequestHandlerBuilder>(request_handler_builder)(),
                                                                                             stop_source.get_token()));
                }
            } catch (std::system_error const &error) {
                if (error.code() != std::errc::operation_canceled) {
                    spdlog::error("server error '{}'", error.code().message());
                    throw;
                }
            }
        }

        template<typename Socket_>
        inline task<http::server_session<Socket_>> tag_invoke(tag_t<web::upgrade_connection>, web::proto::http_,
                                                              http::server_session<Socket_> &session) {
            co_return std::move(session);
        }

    }// namespace http

}// namespace g6
