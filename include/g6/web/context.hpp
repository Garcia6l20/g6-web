#pragma once

#include <g6/io/context.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

namespace g6::web {

    class context : public g6::io::context
    {
        friend auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::http_ const &,
                               net::ip_endpoint);

        friend auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::http_ const &,
                               net::ip_endpoint, const auto &, const auto &);

        friend auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::ws_ const &,
                               net::ip_endpoint);

        friend auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &, web::proto::wss_ const &,
                               net::ip_endpoint, const auto &, const auto &);
    };

}// namespace g6::web

#include <g6/web/impl/context.hpp>
