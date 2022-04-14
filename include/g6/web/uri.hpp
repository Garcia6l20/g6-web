/** @file cppcoro/http/url_encode.hpp
 * @author Sylvain Garcia <garcia.6l20@gmail.com>
 */
#pragma once

#include <ctre.hpp>

#include <fmt/format.h>

#include <g6/net/ip_endpoint.hpp>

#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

namespace g6::web {
    class uri
    {
        std::string uri_;

        struct impl_t {
            static constexpr ctll::fixed_string regex =
                R"(^(([^:/?#]+):)?(//(([^/:?#]*))(:([0-9]+))?)?([^?#]*)(\?([^#]*))?(#(.*))?)";
            using builder_type = ctre::regex_builder<regex>;
            static constexpr inline auto match = ctre::regex_match_t<typename builder_type::type>();
        } uri_impl;

    public:
        std::string_view scheme{};
        std::string_view host{};
        std::string_view port{};
        std::string_view path{};
        std::string_view parameters{};

        uri(std::string_view input) noexcept : uri_{std::move(input)} {
            if (auto m = uri_impl.match(uri_); m) {
                scheme = m.get<2>();
                host = m.get<5>();
                port = m.get<7>();
                path = m.get<8>();
                parameters = m.get<12>();
            }
        }

        [[nodiscard]] std::optional<net::ip_endpoint> endpoint() const {
            return net::ip_endpoint::from_string(fmt::format("{}:{}", host, port));
        }

        [[nodiscard]] bool uses_ssl() const noexcept {
            static std::vector ssl_schemes{"https", "wss"};
            return std::find(std::begin(ssl_schemes), std::end(ssl_schemes), scheme) != std::end(ssl_schemes);
        }

        static auto escape(std::string_view input) {
            std::string output{};
            output.reserve(input.size());
            for (size_t ii = 0; ii < input.size(); ++ii) {
                static constexpr ctll::fixed_string printable_chars = R"([a-zA-Z0-9])";
                if (not ctre::match<printable_chars>(std::string_view{&input[ii], 1})) {
                    output.reserve(output.capacity() + 3);
                    output.append(fmt::format(FMT_STRING("%{:02X}"), uint8_t(input[ii])));
                } else {
                    output.append(std::string_view{&input[ii], 1});
                }
            }
            return output;
        }
        static auto unescape(std::string_view input) {
            std::string output{};
            output.reserve(input.size());
            for (size_t ii = 0; ii < input.size(); ++ii) {
                static constexpr ctll::fixed_string two_printable_chars = R"(%[a-zA-Z0-9]{2})";
                if (ctre::match<two_printable_chars>(std::string_view{&input[ii], 3})) {
                    uint8_t c{};
                    std::from_chars(&input[ii + 1], &input[ii + 3], c, 16);
                    output.append(std::string_view{reinterpret_cast<char *>(&c), 1});
                    ii += 2;
                } else {
                    output.append(std::string_view{&input[ii], 1});
                }
            }
            return output;
        }
    };
}// namespace g6::web
