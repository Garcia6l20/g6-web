#pragma once

#include <g6/http/http.hpp>
#include <g6/net/net_cpo.hpp>
#include <g6/utils/c_ptr.hpp>
#include <g6/web/uri.hpp>

#include <g6/coro/generator.hpp>

#include <fmt/format.h>

#include <charconv>
#include <concepts>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace g6::http::detail {
    template<bool is_request>
    class static_parser_handler;

    template<bool is_request_>
    inline bool tag_invoke(tag_t<g6::net::has_pending_data>,
                           g6::http::detail::static_parser_handler<is_request_> &sph) noexcept;

    template<bool is_request>
    class static_parser_handler {
        using self_type = static_parser_handler<is_request>;

    protected:
        using method_or_status_t = std::conditional_t<is_request, http::method, http::status>;

        static_parser_handler() noexcept;
        ~static_parser_handler() noexcept;
        static_parser_handler(static_parser_handler &&other) noexcept;
        static_parser_handler &operator=(static_parser_handler &&other) noexcept;
        static_parser_handler(const static_parser_handler &) noexcept = delete;
        static_parser_handler &operator=(const static_parser_handler &) noexcept = delete;

        std::optional<size_t> content_length() const noexcept {
            if (headers_.contains("Content-Length")) {
                auto it = headers_.find("Content-Length");
                std::span view{it->second.data(), it->second.size()};
                size_t sz = 0;
                auto [ptr, error] = std::from_chars(view.data(), view.data() + view.size(), sz);
                assert(error == std::errc{});
                return sz;
            } else {
                return {};
            }
        }

        [[nodiscard]] bool header_done() const noexcept { return state_ >= parser_status::on_headers_complete; }
        [[nodiscard]] auto const&body() { return body_; }

    public:
        bool parse(std::span<std::byte const> data);

        [[nodiscard]] bool chunked() const;

        friend bool tag_invoke(tag_t<g6::net::has_pending_data>,
                               g6::http::detail::static_parser_handler<is_request> &sph) noexcept {
            return sph.state_ != parser_status::on_message_complete;
        }

        http::method method() const;
        http::status status_code() const;

        method_or_status_t status_code_or_method() const {
            if constexpr (is_request) {
                return method();
            } else {
                return status_code();
            }
        }

        const auto &url() const { return url_; }
        auto uri() const { return web::uri{url_}; }

        auto &url() { return url_; }

        std::string to_string() const;

        auto &headers() { return headers_; }

        auto &header(const std::string &key) {
            auto it = headers_.find(key);
            if (it == headers_.end()) {
                return headers_.emplace(key, "")->second;
            } else {
                return it->second;
            }
        }

        auto const &header_at(const std::string &key) const {
            auto it = headers_.find(key);
            if (it == headers_.end()) {
                throw std::out_of_range("header not found: " + key);
            } else {
                return it->second;
            }
        }

        std::optional<std::string_view> get_header(std::string_view key) const {
            auto it = std::find_if(begin(headers_), end(headers_), [&](auto const &elem) { return elem.first == key; });
            if (it == headers_.end()) {
                return std::nullopt;
            } else {
                return std::string_view{it->second.data(), it->second.size()};
            }
        }

        g6::generator<std::string_view> get_headers(std::string_view key) {
            auto it = begin(headers_);
            while (it != end(headers_)) {
                it = std::find_if(it, end(headers_), [&](auto const &elem) { return elem.first == key; });
                if (it == end(headers_)) { break; }
                co_yield std::string_view{it->second.data(), it->second.size()};
                ++it;
            }
        }

        static constexpr std::string_view ltrim(std::string_view input) {
            input.remove_prefix(std::min(input.find_first_not_of(" "), input.size()));
            return input;
        }

        static constexpr std::string_view rtrim(std::string_view input) {
            auto end = input.find_last_not_of(" ");
            if (end != std::string_view::npos) { input.remove_suffix(input.size() - end - 1); }
            return input;
        }
        static constexpr std::string_view trim(std::string_view input) { return rtrim(ltrim(input)); }

        auto cookies() const {
            std::map<std::string_view, std::string_view> result{};
            if (auto hdr = get_header("Cookie"); hdr) {//
                for (auto [k, v] : *hdr | std::views::split(';') | std::views::transform([](auto &&rng) {
                         auto kv = std::string_view{&*rng.begin(), size_t(std::ranges::distance(rng))}
                                 | std::views::split('=') | std::views::transform([](auto &&rng) {
                                       return std::string_view{&*rng.begin(), size_t(std::ranges::distance(rng))};
                                   });
                         auto it = kv.begin();
                         return std::pair{*it, *(++it)};
                     })) {
                    result[trim(k)] = trim(v);
                }
            }
            return result;
        }

    protected:
        enum class parser_status {
            none,
            on_message_begin,
            on_url,
            on_status,
            on_header_field,
            on_header_value,
            on_headers_complete,
            on_body,
            on_message_complete,
            on_chunk_header,
            on_chunk_header_compete,
        };
        void *c_parser_ = nullptr;

        static int on_message_begin(void *parser);
        static int on_url(void *parser, const char *data, size_t len);
        static int on_status(void *parser, const char *data, size_t len);
        static int on_header_field(void *parser, const char *data, size_t len);
        static int on_header_value(void *parser, const char *data, size_t len);
        static int on_headers_complete(void *parser);
        static int on_body(void *parser, const char *data, size_t len);
        static int on_message_complete(void *parser);
        static int on_chunk_header(void *parser);
        static int on_chunk_complete(void *parser);

        size_t execute_parser(const char *data, size_t len);

        parser_status status() const noexcept {
            return state_;
        }

    private:
        parser_status state_{parser_status::none};
        std::string header_field_;
        std::string url_;
        std::span<std::byte> body_;
        http::headers headers_;
    };
}// namespace g6::http::detail