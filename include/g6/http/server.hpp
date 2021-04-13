#pragma once

#include <g6/http/http.hpp>
#include <g6/http/session.hpp>

#include <g6/net/async_socket.hpp>
#include <g6/net/ip_endpoint.hpp>
#include <g6/net/tcp.hpp>

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

    template<typename Context>
    auto tag_invoke(tag_t<make_server>, Context &, web::proto::http_ const &, net::ip_endpoint endpoint);

    template<typename Context>
    auto tag_invoke(tag_t<make_server>, Context &ctx, web::proto::https_ const &, net::ip_endpoint endpoint,
                    const auto &, const auto &);

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

            server() = delete;
            server(server const &) = delete;
            server(server &&) noexcept = default;
            ~server() noexcept {
              // cleanup operation is not stoppable it must be done separately
              sync_wait(scope_.cleanup());
            }

            template<typename Context2>
            friend auto g6::tag_invoke(tag_t<make_server>, Context2 &, web::proto::http_ const &,
                                       net::ip_endpoint endpoint);

            template<typename Context2>
            friend auto g6::tag_invoke(tag_t<make_server>, Context2 &ctx, web::proto::https_ const &, net::ip_endpoint,
                                       const auto &, const auto &);

            friend auto tag_invoke(tag_t<make_session>, http::server<Context, Socket> const &, Socket &&sock,
                                   net::ip_endpoint &&endpoint) noexcept {
                return just(
                    server_session<Socket>{std::forward<Socket>(sock), std::forward<net::ip_endpoint>(endpoint)});
            }

            template<typename Server, typename RequestHandlerBuilder>
            friend auto tag_invoke(tag_t<async_serve>, Server &server, inplace_stop_source &stop_source,
                                         RequestHandlerBuilder &&request_handler_builder) {
                auto receiver = transform(
                    [&server = server, &scope = server.scope_, &ctx = server.context_, stop_source = &stop_source,
                     builder = std::forward<RequestHandlerBuilder>(request_handler_builder)](auto result_tuple) {
                        auto &[peer, peer_address] = result_tuple;
                        spdlog::info("Client connected: {}", peer_address.to_string());
                        scope.spawn(
                            with_query_value(
                                let(make_session(server, std::move(peer), std::move(peer_address)),
                                    [stop_source, builder = std::forward<decltype(builder)>(builder)](auto &session) {
                                        return let(just(), [&]() mutable {
                                            auto request_handler = builder(session);
                                            return let(//net::async_recv(*session),
                                                net::async_recv(session), [request_handler](auto request) mutable {
                                                    return request_handler(request.get());
                                                });
                                        });
                                    }),
                                get_stop_token, stop_source->get_token()),
                            ctx.get_scheduler());
                    });
                return with_query_value(
                    let(just(),
                        [stop_source = &stop_source, server = &server, receiver = std::move(receiver)] {
                            return net::async_accept(server->socket) | std::move(receiver);
                        })
                        | repeat_effect() | transform_done([&]() noexcept { return just(); }),
                    get_stop_token, stop_source.get_token());
            }
        };
    }// namespace http

    template<typename Context>
    auto tag_invoke(tag_t<make_server>, Context &ctx, web::proto::http_ const &, net::ip_endpoint endpoint) {
        auto socket = net::open_socket(ctx, net::tcp_server, std::move(endpoint));
        return http::server{ctx, std::move(socket)};
    }

    template<typename Context>
    auto tag_invoke(tag_t<make_server>, Context &ctx, web::proto::https_ const &, net::ip_endpoint endpoint,
                    const auto &cert, const auto &key) {
        auto socket = net::open_socket(ctx, ssl::tcp_server, std::move(endpoint), cert, key);
        return http::server{ctx, std::move(socket)};
    }
}// namespace g6
