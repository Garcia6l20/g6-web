#pragma once

#include <g6/http/http.hpp>
#include <g6/http/session.hpp>

#include <g6/net/async_socket.hpp>
#include <g6/net/ip_endpoint.hpp>
#include <g6/net/tcp.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

#include <unifex/async_scope.hpp>
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/transform_done.hpp>

#include <spdlog/spdlog.h>

namespace g6 {

template <typename Context>
auto tag_invoke(tag_t<make_server>, Context &, web::proto::http_ const &,
                net::ip_endpoint endpoint);

namespace http {

template <typename Context, typename Socket> class server {
  Context &context_;
  Socket socket_;
  async_scope scope_{};
  server(Context &context, Socket socket)
      : context_{context}, socket_{std::move(socket)} {}

public:

  auto local_endpoint() const {
    return socket_.local_endpoint();
  }

  template <typename Context2>
  friend auto g6::tag_invoke(tag_t<make_server>, Context2 &,
                             web::proto::http_ const &,
                             net::ip_endpoint endpoint);

  template <typename RequestHandlerBuilder>
  friend auto tag_invoke(tag_t<async_serve>, server &&server, auto stop_token,
                         RequestHandlerBuilder &&request_handler_builder) {
    return with_query_value(let(just(),
               [&] {
                 return net::async_accept(server.socket_) |
                        transform(
                            [&scope = server.scope_, &ctx = server.context_,
                             builder =
                                 std::forward<RequestHandlerBuilder>(request_handler_builder)](
                                auto peer, auto peer_address) {
                              spdlog::info("Client connected: {}",
                                           peer_address.to_string());
                              scope.spawn(let(just(),
                                              [session = std::make_shared<http::server_session<decltype(server.socket_)>>(
                                                  std::move(peer), std::move(peer_address)),
                                               builder =
                                                   std::forward<decltype(builder)>(
                                                       builder)] {
                                                return let(just(), [&] {
                                                  auto request_handler = builder(*session);
                                                  return let(net::async_recv(*session), [session=session, request_handler](auto request) mutable {
                                                    return request_handler(request.get());
                                                  });
                                                });
                                              }),
                                          ctx.get_scheduler());
                            });
               }) |
           repeat_effect_until([stop_token = stop_token]() {
             return stop_token.stop_requested();
           }) |
           transform_done([] {
             return just();
           }), get_stop_token, stop_token);
  }
};
} // namespace http

template <typename Context>
auto tag_invoke(tag_t<make_server>, Context &ctx, web::proto::http_ const &,
                net::ip_endpoint endpoint) {
  auto socket = net::open_socket(ctx, net::tcp_server, std::move(endpoint));
  return http::server{ctx, std::move(socket)};
}

} // namespace g6
