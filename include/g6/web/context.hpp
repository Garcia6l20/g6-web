#pragma once

#include <g6/io/context.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

namespace g6::web {

    class context : public g6::io::context
    {
        template<typename Context>
        friend auto tag_invoke(tag<g6::web::make_server>, Context &, net::ip_endpoint endpoint);

        template<typename Context>
        friend auto tag_invoke(tag<g6::web::make_server>, Context &ctx, net::ip_endpoint endpoint, const auto &,
                               const auto &);
    };

}// namespace g6::web

#include <g6/web/impl/context.hpp>
