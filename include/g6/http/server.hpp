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

        auto tag_invoke(tag<g6::web::make_server>, g6::web::context &, net::ip_endpoint);

        auto tag_invoke(tag<g6::web::make_server>, g6::web::context &, net::ip_endpoint, const auto &, const auto &);
    }// namespace web

    namespace http {

        template<typename Context, typename Socket>
        class server
        {
        protected:
            Context &context_;
            ff_spawner scope_{};
            server(Context &context, Socket socket) : context_{context}, socket{std::move(socket)} {}

        public:
            static constexpr auto proto = web::proto::http;

            Socket socket;

            using connection_type = server_session<Socket>;

            server() = delete;
            server(server const &) = delete;
            server(server &&) noexcept = default;
            ~server() = default;

            friend auto g6::web::tag_invoke(tag<g6::web::make_server>, g6::web::context &, net::ip_endpoint);

            friend auto g6::web::tag_invoke(tag<g6::web::make_server>, g6::web::context &, net::ip_endpoint,
                                            const auto &, const auto &);

            template<template<class, class> typename Server, typename Context_, typename Socket_,
                     typename RequestHandlerBuilder>
            friend inline task<void> tag_invoke(tag<g6::web::async_serve>, Server<Context_, Socket_> &server,
                                                std::stop_source &stop_source,
                                                RequestHandlerBuilder &&request_handler_builder) try {

                auto &scope = server.scope_;

                while (not stop_source.stop_requested()) {
                    auto [sock, address] = co_await net::async_accept(server.socket, stop_source.get_token());
                    auto http_session = server_session<Socket_>{std::move(sock), address};
                    spdlog::info("client connected: {}", address.to_string());
                    auto session = co_await web::upgrade_connection(Server<Context_, Socket_>::proto, http_session);
                    scope.spawn([](auto session, auto builder) mutable -> task<void> {
                        try {
                            auto request_handler = builder();
                            auto request = co_await net::async_recv(session);
                            co_await request_handler(session, std::move(request));
                        } catch (std::system_error const &error) {
                            if (error.code() != std::errc::connection_reset) {
                                spdlog::error("connection {} error '{}'", session.remote_endpoint().to_string(),
                                              error.code().message());
                                throw;
                            }
                            spdlog::info("connection reset '{}'", session.remote_endpoint().to_string());
                        }
                    }(std::move(session), std::forward<RequestHandlerBuilder>(request_handler_builder)));
                }
            } catch (std::system_error const &error) {
                if (error.code() != std::errc::operation_canceled) {
                    spdlog::error("server error '{}'", error.code().message());
                    throw;
                }
            }
        };

        template<typename Socket_>
        inline task<http::server_session<Socket_>> tag_invoke(tag<web::upgrade_connection>, web::proto::http_,
                                                              http::server_session<Socket_> &session) {
            co_return std::move(session);
        }

    }// namespace http

    namespace io {
    }// namespace io

    namespace web {

    }// namespace web

}// namespace g6
