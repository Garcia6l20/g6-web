/**
 * @file g6/http/message.hpp
 * @author Garcia Sylvain <garcia.6l20@gmail.com>
 */
#pragma once

#include <g6/http/impl/static_parser_handler.hpp>

#include <fmt/format.h>
#include <span>

namespace g6::http {

#if 0
    namespace detail {
        enum
        {
            max_body_size = 1024
        };

        struct base_message {
            base_message() = default;

            explicit base_message(http::headers &&headers) : headers{std::forward<http::headers>(headers)} {}

            base_message(base_message const &other) = delete;

            base_message &operator=(base_message const &other) = delete;

            base_message(base_message &&other) = default;

            base_message &operator=(base_message &&other) = default;

            std::optional<std::reference_wrapper<std::string>> header(const std::string &key) noexcept {
                if (auto it = headers.find(key); it != headers.end()) { return it->second; }
                return {};
            }

            http::headers headers;
            byte_span body;

            template <size_t extent>
            friend task<size_t> tag_invoke(tag_t<net::async_recv>, server_request &request, std::span<std::byte, extent> data, std::stop_token stop = {}) {
                size_t body_sz = request.body().size();
                if (body_sz > body_offset_) {
                    size_t copy_sz = std::min(data.size(), request.body().size());
                    std::memcpy(data.data(), request.body().data(), copy_sz);
                    body_offset_ += copy_sz;
                    co_return copy_sz;
                } else {
                    size_t bytes = co_await net::async_recv(request.socket_, data);
                    if (bytes == 0) { throw std::system_error{std::make_error_code(std::errc::connection_reset)}; }
                    request.parse(data);
                    co_return request.body();
                }
            }

            //			virtual bool is_chunked() = 0;
            //			virtual std::string build_header() = 0;
            //			virtual task<std::span<std::byte, std::dynamic_extent>> read_body(size_t
            //max_size = max_body_size) = 0; 			virtual task<size_t> write_body(std::span<std::byte,
            //std::dynamic_extent> data) = 0;
        };

        struct base_request : base_message {
            static constexpr bool is_request = true;
            static constexpr bool is_response = false;
            using base_message::base_message;

            base_request(base_request &&other) = default;
            base_request &operator=(base_request &&other) = default;

            base_request(http::method method, std::string &&path, http::headers &&headers = {})
                : base_message{std::forward<http::headers>(headers)}, method{method}, path{std::forward<std::string>(
                                                                                          path)} {}

            http::method method;
            std::string path;

            [[nodiscard]] auto method_str() const { return http_method_str(static_cast<detail::http_method>(method)); }

            std::string to_string() const { return fmt::format("{} {}", method_str(), path); }
        };

        struct base_response : base_message {
            static constexpr bool is_response = true;
            static constexpr bool is_request = false;
            using base_message::base_message;

            base_response(base_response &&other) = default;
            base_response &operator=(base_response &&other) = default;

            base_response(http::status status, http::headers &&headers = {})
                : base_message{std::forward<http::headers>(headers)}, status{status} {}

            http::status status;

            [[nodiscard]] auto status_str() const { return http_status_str(static_cast<detail::http_status>(status)); }

            std::string to_string() const { return status_str(); }
        };

        template<bool _is_response, is_body BodyT>
        struct abstract_message : std::conditional_t<_is_response, base_response, base_request> {
            using base_type = std::conditional_t<_is_response, base_response, base_request>;
            static constexpr bool is_response = _is_response;
            static constexpr bool is_request = !_is_response;
            using parser_type = http::detail::static_parser_handler<is_request>;

            using base_type::base_type;

            std::optional<async_generator<std::string_view>> chunk_generator_;
            std::optional<async_generator<std::string_view>::iterator> chunk_generator_it_;

            abstract_message(abstract_message::parser_type &parser)
                : base_response{parser.template status_code_or_method<is_request>(), std::move(parser.headers_),
                                std::move(parser.url()), std::move(parser.header_)} {}

            abstract_message(auto status_or_method, http::headers &&headers = {})
                : base_response{status_or_method, std::forward<http::headers>(headers)} {}

            abstract_message(http::method method, std::string &&path, http::headers &&headers = {}) requires(is_request)
                : base_request{method, std::forward<std::string>(path), std::forward<http::headers>(headers)} {}

            auto &operator=(parser_type &parser) noexcept requires(is_request) {
                this->method = parser.method();
                this->path = std::move(parser.url());
                this->headers = std::move(parser.headers_);
                return *this;
            }

            inline std::string build_header() {
                for (auto &[k, v] : this->headers) { spdlog::debug("- {}: {}", k, v); }
                std::string output = _header_base();
                auto write_header = [&output](const std::string &field, const std::string &value) {
                    output += fmt::format("{}: {}\r\n", field, value);
                };
                if constexpr (ro_basic_body<BodyT>) {
                    auto sz = this->body.size();
                    if (auto node = this->headers.extract("Content-Length"); !node.empty()) {
                        node.mapped() = std::to_string(sz);
                        this->headers.insert(std::move(node));
                    } else {
                        this->headers.emplace("Content-Length", std::to_string(sz));
                    }
                } else if constexpr (ro_chunked_body<BodyT>) {
                    this->headers.emplace("Transfer-Encoding", "chunked");
                }
                for (auto &[field, value] : this->headers) { write_header(field, value); }
                output += "\r\n";
                return output;
            }

        private:
            inline auto _header_base() {
                if constexpr (is_response) {
                    return fmt::format("HTTP/1.1 {} {}\r\n"
                                       "UserAgent: g6-http/0.0\r\n",
                                       int(this->status), http_status_str(this->status));
                } else {
                    return fmt::format("{} {} HTTP/1.1\r\n"
                                       "UserAgent: g6-http/0.0\r\n",
                                       this->method_str(), this->path);
                }
            }
        };
    }// namespace detail
#endif

    namespace detail {

        template<typename Super>
        struct message_builder_base {


        private:
            auto &super() { return *static_cast<Super *>(this); }
            auto const &super() const { return *static_cast<Super const *>(this); }

            // send header
            friend task<size_t> tag_invoke(tag_t<net::async_send>, message_builder_base &self) {
                std::string header = self._header_base();
                for (const auto &[field, value] : self.super().headers) {
                    format_to(std::back_inserter(header), "{}: {}\r\n", field, value);
                }
                header += "\r\n";
                self.header_sent_ = true;
                co_return co_await net::async_send(self.super().socket, header);
            }

            // send data
            template<size_t extent>
            friend auto tag_invoke(tag_t<net::async_send>, message_builder_base &self,
                                           std::span<std::byte const, extent> data) {
                return net::async_send(self.super().socket, data);
            }

        protected:
            inline std::string _header_base() const {
                if constexpr (Super::is_request) {
                    return format("{} {} HTTP/1.1\r\n"
                                  "UserAgent: g6-http/0.0\r\n",
                                  to_string(super().method), super().path);
                } else {
                    return format("HTTP/1.1 {} {}\r\n"
                                  "UserAgent: g6-http/0.0\r\n",
                                  int(super().status), to_string(super().status));
                }
            }

            bool header_sent_ = false;
        };

        template<typename Socket>
        struct request_builder : message_builder_base<request_builder<Socket>> {
            static constexpr bool is_request = true;
            Socket &socket;
            http::method method;
            std::string path;
            http::headers headers;
            request_builder(Socket &socket_, http::method method_, std::string_view path_,
                            http::headers headers_) noexcept
                : socket{socket_}, method{method_}, path{path_}, headers{std::move(headers_)} {}
        };

        template<typename Socket>
        struct response_builder : message_builder_base<request_builder<Socket>> {
            static constexpr bool is_request = false;
            Socket &socket;
            http::status status;
            http::headers headers;
        };


        template<typename Socket, bool is_request>
        struct parser_base : detail::static_parser_handler<is_request> {

            Socket &socket_;

            parser_base(Socket &sock) noexcept : socket_{sock} {}
            parser_base(parser_base const &) = delete;
            parser_base(parser_base &&other) noexcept
                : detail::static_parser_handler<is_request>{std::move(other)}, socket_{other.socket_} {}

            template<size_t extent>
            friend task<size_t> tag_invoke(tag_t<net::async_recv>, parser_base &self, std::span<std::byte, extent> data,
                                           std::stop_token stop = {}) {
                size_t bytes_received;
                do {
                    size_t bytes_received = co_await net::async_recv(self.socket_, data);
                    if (bytes_received == 0) {//
                        throw std::system_error{std::make_error_code(std::errc::connection_reset)};
                    }
                    self.parse(std::span{data.data(), bytes_received});
                } while (self.status() < parser_base::parser_status::on_body);
                if (not self.body().empty()) {
                    // move body to begining of data
                    std::memmove(data.data(), self.body().data(), self.body().size());
                }
                co_return self.body().size();
            }
        };

        template<typename Socket>
        using request_parser = detail::parser_base<Socket, true>;

        template<typename Socket>
        using response_parser = detail::parser_base<Socket, false>;
    }// namespace detail

}// namespace g6::http
