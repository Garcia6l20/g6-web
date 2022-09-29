/** @file g6/web/uri.hpp
 * @author Sylvain Garcia <garcia.6l20@gmail.com>
 */
#pragma once

#include <g6/coro/config.hpp>

#include <fmt/format.h>

#include <g6/net/ip_endpoint.hpp>

namespace g6::web {
    class uri {
        std::string uri_;

    public:
        std::string_view scheme{};
        std::string_view host{};
        std::string_view port{};
        std::string_view path{};
        std::string_view parameters{};

        uri(std::string_view input) noexcept;
        uri(uri const&) = delete;
        uri(uri &&) = delete;
        uri &operator=(uri const&) = delete;
        uri &operator=(uri &&) = delete;

        [[nodiscard]] std::optional<net::ip_endpoint> endpoint() const;

        [[nodiscard]] bool uses_ssl() const noexcept;

        static std::string escape(std::string_view input);
        static std::string unescape(std::string_view input);

        template<typename Context>
        friend auto tag_invoke(tag_t<g6::format_to>, uri const& self, Context &ctx) {
            return g6::format_to(ctx.out(), "{}", self.uri_);
        }
    };

}// namespace g6::web
