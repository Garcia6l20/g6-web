#pragma once

#include <g6/http/http.hpp>
#include <g6/http/session.hpp>

#include <g6/net/async_socket.hpp>
#include <g6/net/ip_endpoint.hpp>

#include <g6/ssl/async_socket.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

#include <unifex/async_scope.hpp>
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/transform_done.hpp>

#include <spdlog/spdlog.h>

namespace g6 {

    namespace web {
        template<typename Context>
        auto tag_invoke(tag_t<g6::web::make_server>, Context &, web::proto::http_ const &, net::ip_endpoint endpoint);

        template<typename Context>
        auto tag_invoke(tag_t<g6::web::make_server>, Context &ctx, web::proto::https_ const &,
                        net::ip_endpoint endpoint, const auto &, const auto &);
    }// namespace web

    namespace http {

        template<typename Context, typename Socket>
        class server
        {
        protected:
            Context &context_;
            async_scope scope_{};
            server(Context &context, Socket socket) : context_{context}, socket{std::move(socket)} {}

        public:
            Socket socket;

            using connection_type = server_session<Socket>;

            server() = delete;
            server(server const &) = delete;
            server(server &&) noexcept = default;
            ~server() noexcept {
                // cleanup operation is not stoppable it must be done separately
                sync_wait(scope_.cleanup());
            }

            template<typename Context2>
            friend auto g6::web::tag_invoke(tag_t<g6::web::make_server>, Context2 &, web::proto::http_ const &,
                                            net::ip_endpoint endpoint);

            template<typename Context2>
            friend auto g6::web::tag_invoke(tag_t<g6::web::make_server>, Context2 &ctx, web::proto::https_ const &,
                                            net::ip_endpoint, const auto &, const auto &);

            //            friend auto tag_invoke(tag_t<web::upgrade_connection>, http::server<Context, Socket> const &, Socket &&sock,
            //                                   net::ip_endpoint &&endpoint) noexcept {
            //                return just(
            //                    server_session<Socket>{std::forward<Socket>(sock), std::forward<net::ip_endpoint>(endpoint)});
            //            }

            //            template<typename UpgradedConnection>
            //            task<UpgradedConnection> tag_invoke(unifex::tag_t<web::upgrade_connection>, Context &context,
            //                                                http::server_session<Socket> &session,
            //                                                std::type_identity<UpgradedConnection> conn_type) {
            //                co_return co_await web::upgrade_connection(conn_type, context, session, session.socket);
            //            }

            template<typename Server, typename RequestHandlerBuilder>
            friend task<void> tag_invoke(tag_t<web::async_serve>, Server &server, inplace_stop_source &stop_source,
                                         RequestHandlerBuilder &&request_handler_builder) {
                while (not stop_source.stop_requested()) {
                    auto [sock, address] = co_await with_query_value(net::async_accept(server.socket), get_stop_token,
                                                                     stop_source.get_token());
                    auto http_session = server_session<Socket>{std::move(sock), address};
                    spdlog::info("client connected: {}", address.to_string());
                    auto session = co_await web::upgrade_connection(server, http_session);
                    server.scope_.spawn(
                        with_query_value(let(just(),// TODO why this let just ? (task does not have sends_done ?)
                                             [session = std::move(session),
                                              builder = std::forward<RequestHandlerBuilder>(
                                                  request_handler_builder)]() mutable -> task<void> {
                                                 auto request_handler = builder(session);
                                                 auto request = co_await net::async_recv(session);
                                                 co_await request_handler(std::move(request));
                                             }),
                                         get_stop_token, stop_source.get_token()),
                        server.context_.get_scheduler());
                }
            }
        };
    }// namespace http

    namespace io {
        template<typename Context_, typename Socket_>
        task<http::server_session<Socket_>> tag_invoke(unifex::tag_t<web::upgrade_connection>,
                                                       http::server<Context_, Socket_> &,
                                                       http::server_session<Socket_> &session) {
            co_return std::move(session);
        }
    }

    namespace web {

        template<typename Context>
        auto tag_invoke(tag_t<g6::web::make_server>, Context &ctx, web::proto::http_ const &,
                        net::ip_endpoint endpoint) {
            auto socket = net::open_socket(ctx, net::tcp_server, std::move(endpoint));
            return http::server{ctx, std::move(socket)};
        }

        template<typename Context>
        auto tag_invoke(tag_t<g6::web::make_server>, Context &ctx, web::proto::https_ const &,
                        net::ip_endpoint endpoint, const auto &cert, const auto &key) {
            auto socket = net::open_socket(ctx, ssl::tcp_server, std::move(endpoint), cert, key);
            return http::server{ctx, std::move(socket)};
        }
    }// namespace web

}// namespace g6
