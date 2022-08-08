/** @file g6/web/uri.hpp
 * @author Sylvain Garcia <garcia.6l20@gmail.com>
 */
#pragma once

#include <g6/coro/config.hpp>

#include <ctre.hpp>

#include <fmt/format.h>

#include <g6/net/ip_address.hpp>
#include <g6/net/ip_endpoint.hpp>
#include <g6/byteswap.hpp>

#include <bit>
#include <charconv>
#include <optional>
#include <string_view>
#include <vector>
#include <ranges>
#include <algorithm>

#if G6_OS_LINUX
#include <netdb.h>
#endif

namespace g6::web {
    class uri {
        std::string uri_;

        struct impl_t {
            static constexpr ctll::fixed_string regex =
                R"(^(([^:/?#]+):)?(//(([^/:?#]*))(:([0-9]+))?)?([^?#]*)(\?([^#]*))?(#(.*))?)";
            using builder_type = ctre::regex_builder<regex>;
            static constexpr auto match =
                ctre::regular_expression<typename builder_type::type, ctre::match_method, ctre::singleline>();
        } uri_impl;

    public:
        std::string_view scheme{};
        std::string_view host{};
        std::string_view port{};
        std::string_view path{};
        std::string_view parameters{};

        uri(std::string_view input) noexcept : uri_{input} {
            if (auto m = uri_impl.match(uri_); m) {
                scheme = m.get<2>();
                host = m.get<5>();
                port = m.get<7>();
                if (auto p = m.get<8>(); p.size()) { path = p; }
                if (auto p = m.get<12>(); p.size()) { parameters = p; }
            }
        }
        uri(uri const&) = delete;
        uri(uri &&) = delete;
        uri &operator=(uri const&) = delete;
        uri &operator=(uri &&) = delete;

        [[nodiscard]] std::optional<net::ip_endpoint> endpoint() const {
            auto addr = g6::from_string<net::ip_address>(host);
            if (!addr) {//
                struct hostent *he = nullptr;
                struct hostent he_data = {0};
                char buffer[2048] = {0};
                int errnum;
                char c_str[256];
                std::memcpy(c_str, host.data(), host.size());
                c_str[host.size()] = 0;

#ifdef G6_OS_LINUX
                gethostbyname_r(c_str, &he_data, buffer, sizeof(buffer), &he, &errnum);
#else
                he = gethostbyname_r(c_str, &he_data, buffer, sizeof(buffer), &errnum);
#endif
                if (he == nullptr) { return std::nullopt; }
                if (he->h_addr_list[0][4] == 0) {
                    uint32_t addr_value;
                    std::memcpy(&addr_value, he->h_addr_list[0], sizeof(uint32_t));
                    addr = net::ipv4_address{g6::byteswap(addr_value)};
                } else {
                    uint64_t subnet_prefix;
                    uint64_t interface_identifier;
                    std::memcpy(&subnet_prefix, he->h_addr_list[0], sizeof(uint64_t));
                    std::memcpy(&interface_identifier, he->h_addr_list[0] + sizeof(uint64_t), sizeof(uint64_t));
                    addr = net::ipv6_address{subnet_prefix, interface_identifier};
                }
            }

            uint16_t port_value = 0;
            if (!port.empty()) {
                if (auto parsed_port = from_string<uint16_t>(port); parsed_port) { port_value = *parsed_port; }
            }

            return net::ip_endpoint{*addr, port_value};
        }

        [[nodiscard]] bool uses_ssl() const noexcept {
            static std::vector ssl_schemes{"https", "wss"};
            return std::ranges::find(ssl_schemes, scheme) != std::end(ssl_schemes);
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

        template<typename Context>
        friend auto tag_invoke(tag_t<g6::format_to>, uri const& self, Context &ctx) {
            return g6::format_to(ctx.out(), "{}", self.uri_);
        }
    };
}// namespace g6::web
