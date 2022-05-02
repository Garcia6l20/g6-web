#pragma once

#include <bits/ranges_base.h>
#include <g6/http/http.hpp>
#include <g6/net/net_cpo.hpp>
#include <g6/utils/c_ptr.hpp>
#include <g6/web/uri.hpp>

#include <g6/generator.hpp>

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
    template<detail::http_parser_type type, typename T>
    void init_parser(detail::http_parser *parser, T *owner) {
        detail::http_parser_init(parser, type);
        parser->data = owner;
    }

    template<detail::http_parser_type type, typename OwnerT>
    using c_parser_ptr = g6::c_unique_ptr<detail::http_parser, init_parser<type, OwnerT>>;

    template<bool is_request>
    class static_parser_handler;

    template<bool is_request_>
    inline bool tag_invoke(tag_t<g6::net::has_pending_data>,
                           g6::http::detail::static_parser_handler<is_request_> &sph) noexcept;

    template<bool is_request>
    class static_parser_handler {
        using self_type = static_parser_handler<is_request>;

        static constexpr auto c_parser_type = []() {
            if constexpr (is_request) {
                return detail::http_parser_type::HTTP_REQUEST;
            } else {
                return detail::http_parser_type::HTTP_RESPONSE;
            }
        }();

        using parser_ptr = c_parser_ptr<c_parser_type, static_parser_handler>;

    protected:
        using method_or_status_t = std::conditional_t<is_request, http::method, http::status>;

        static_parser_handler() = default;
        static_parser_handler(static_parser_handler &&other) noexcept
            : parser_{std::move(other.parser_)}, header_field_{std::move(other.header_field_)}, url_{std::move(
                                                                                                    other.url_)},
              body_{std::move(other.body_)}, state_{std::move(other.state_)}, headers_{std::move(other.headers_)} {
            parser_->data = this;
        }

        static_parser_handler &operator=(static_parser_handler &&other) noexcept {
            parser_ = std::move(other.parser_);
            header_field_ = std::move(other.header_field_);
            url_ = std::move(other.url_);
            body_ = std::move(other.body_);
            state_ = std::move(other.state_);
            headers_ = std::move(other.headers_);
            parser_->data = this;
            return *this;
        }
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
        [[nodiscard]] bool has_body() const noexcept { return body_.size(); }
        [[nodiscard]] auto body() { return std::exchange(body_, {}); }
        [[nodiscard]] size_t body_size() const { return body_.size(); }

    public:
        bool parse(std::span<std::byte const> data) {
            body_ = {};
            spdlog::debug("static_parser_handler::parse: {}",
                          std::string_view{reinterpret_cast<const char *>(data.data()), data.size()});
            const auto count = execute_parser(reinterpret_cast<const char *>(data.data()), data.size());
            if (count < data.size()) {
                throw std::runtime_error{fmt::format(FMT_STRING("parse error: {}"),
                                                     http_errno_description(detail::http_errno(parser_->http_errno)))};
            }
            return state_ == parser_status::on_message_complete;
        }

        [[nodiscard]] bool chunked() const {
            if (parser_->uses_transfer_encoding) {
                return header_at("Transfer-Encoding") == std::string_view{"chunked"};
            } else {
                return false;
            }
        }

        friend bool tag_invoke(tag_t<g6::net::has_pending_data>,
                               g6::http::detail::static_parser_handler<is_request> &sph) noexcept {
            return (sph.state_ != parser_status::on_message_complete) || (not sph.body_.empty());
        }

        auto method() const { return static_cast<http::method>(parser_->method); }
        auto status_code() const { return static_cast<http::status>(parser_->status_code); }

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

        std::string to_string() const {
            std::vector<char> out;
            std::string_view type;
            if constexpr (is_request) {
                fmt::format_to(std::back_inserter(out), "request {} {}",
                               detail::http_method_str(detail::http_method(parser_->method)), url_);
            } else {
                fmt::format_to(std::back_inserter(out), "response {} ",
                               detail::http_status_str(detail::http_status(parser_->status_code)));
            }
            fmt::format_to(std::back_inserter(out), "{}", body_);
            return out.data();
        }

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

        inline static auto &instance(detail::http_parser *parser) { return *static_cast<self_type *>(parser->data); }

        static inline int on_message_begin(detail::http_parser *parser) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_message_begin;
            return 0;
        }

        static inline int on_url(detail::http_parser *parser, const char *data, size_t len) {
            auto &this_ = instance(parser);
            this_.url_ = web::uri::unescape({data, len});
            this_.state_ = parser_status::on_url;
            return 0;
        }

        static inline int on_status(detail::http_parser *parser, const char *data, size_t len) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_status;
            return 0;
        }

        static inline int on_header_field(detail::http_parser *parser, const char *data, size_t len) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_header_field;
            this_.header_field_ = {data, len};
            return 0;
        }

        static inline int on_header_value(detail::http_parser *parser, const char *data, size_t len) {
            auto &this_ = instance(parser);
            if (this_.state_ == parser_status::on_header_field) {
                this_.headers_.emplace(this_.header_field_, std::string{data, data + len});
            } else {
                // header has been cut
                auto it = this_.headers_.find(this_.header_field_);
                assert(it != this_.headers_.end());
                it->second.append(std::string_view{data, data + len});
            }
            this_.state_ = parser_status::on_header_value;

            return 0;
        }

        static inline int on_headers_complete(detail::http_parser *parser) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_headers_complete;
            return 0;
        }

        static inline int on_body(detail::http_parser *parser, const char *data, size_t len) {
            auto &this_ = instance(parser);
            this_.body_ = std::as_writable_bytes(std::span{const_cast<char *>(data), len});
            this_.state_ = parser_status::on_body;
            return 0;
        }

        static inline int on_message_complete(detail::http_parser *parser) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_message_complete;
            return 0;
        }

        static inline int on_chunk_header(detail::http_parser *parser) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_chunk_header;
            return 0;
        }

        static inline int on_chunk_complete(detail::http_parser *parser) {
            auto &this_ = instance(parser);
            this_.state_ = parser_status::on_chunk_header_compete;
            return 0;
        }

        auto execute_parser(const char *data, size_t len) {
            return http_parser_execute(parser_.get(), &http_parser_settings_, data, len);
        }

    private:
        parser_ptr parser_ = parser_ptr::make(this);
        inline static detail::http_parser_settings http_parser_settings_ = {
            on_message_begin,    on_url,  on_status,           on_header_field, on_header_value,
            on_headers_complete, on_body, on_message_complete, on_chunk_header, on_chunk_complete,
        };
        parser_status state_{parser_status::none};
        std::string header_field_;
        std::string url_;
        std::span<std::byte> body_;
        http::headers headers_;

        //		template<bool _is_response, is_body BodyT>
        //		friend struct abstract_message;
        //
        //		template<net::is_socket, net::message_direction,
        //net::connection_mode> 		friend struct cppcoro::http::message;
        //
        //		template<typename, bool>
        //		friend struct cppcoro::http::message_header;
    };
}// namespace g6::http::detail