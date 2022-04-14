#pragma once

#include <g6/http/server.hpp>
#include <g6/web/context.hpp>

#include <g6/net/async_socket.hpp>

namespace g6::web {

    template<typename Context>
    inline auto tag_invoke(tag<g6::web::make_server>, Context &ctx, net::ip_endpoint endpoint) {
        auto socket = net::open_socket(ctx, net::proto::tcp);
        socket.bind(endpoint);
        socket.listen();
        return http::server{ctx, std::move(socket)};
    }

    template<typename Context>
    inline auto tag_invoke(tag<g6::web::make_server>, Context &ctx, net::ip_endpoint endpoint, const auto &cert,
                           const auto &key) {
        auto socket = net::open_socket(ctx, ssl::tcp_server, std::move(endpoint), cert, key);
        return http::server{ctx, std::move(socket)};
    }
}// namespace g6::web
