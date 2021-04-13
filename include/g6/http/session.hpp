#pragma once

#include <unifex/any_sender_of.hpp>
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/sequence.hpp>
#include <unifex/span.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/task.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_done.hpp>

#include <g6/http/impl/static_parser_handler.hpp>
#include <g6/net/ip_endpoint.hpp>
#include <g6/net/net_cpo.hpp>

namespace g6::http {

    template<typename Socket>
    class server_session
    {
    public:
        struct request;

    private:
        Socket socket_;
        net::ip_endpoint endpoint_;
        using session_buffer = std::array<char, 1024>;
        session_buffer buffer_;
        std::string header_data_;

        request request_{socket_, buffer_};

        void build_header(http::status status, http::headers &&headers) noexcept {
            header_data_ = fmt::format("HTTP/1.1 {} {}\r\n"
                                       "UserAgent: g6-http/0.0\r\n",
                                       int(static_cast<detail::http_status>(status)),
                                       detail::http_status_str(static_cast<detail::http_status>(status)));

            for (auto &[field, value] : headers) { header_data_ += fmt::format("{}: {}\r\n", field, value); }
            header_data_ += "\r\n";
        }

    public:
        server_session(Socket socket, net::ip_endpoint endpoint) noexcept
            : socket_{std::move(socket)}, endpoint_{std::move(endpoint)} {}

        friend auto tag_invoke(unifex::tag_t<net::async_recv>, server_session &session) {
            using namespace unifex;
            return net::async_recv(session.socket_, as_writable_bytes(span{session.buffer_}))
                 | transform([&](size_t bytes) {
                       session.request_.parse(span{session.buffer_.data(), bytes});
                       return std::ref(session.request_);
                   });
        }

        template<typename T, size_t extent = unifex::dynamic_extent>
        friend auto tag_invoke(unifex::tag_t<net::async_send>, server_session &session, http::status status,
                               http::headers &&hdrs, unifex::span<T, extent> data) {
            using namespace unifex;
            if (data.size()) { hdrs.template emplace("Content-Length", std::to_string(data.size())); }
            session.build_header(status, std::move(hdrs));
            return let(net::async_send(session.socket_,
                                       as_bytes(span{session.header_data_.data(), session.header_data_.size()})),
                       [data, &session](size_t) {
                           return net::async_send(session.socket_, as_bytes(span{data.data(), data.size()}));
                       });
        }

        template<typename T, size_t extent = unifex::dynamic_extent>
        friend auto tag_invoke(unifex::tag_t<net::async_send> const &tag, server_session &session, http::status status,
                               unifex::span<T, extent> data) {
            using namespace unifex;
            return tag_invoke(tag, session, status, http::headers{},
                              data);
        }

        struct response_stream {
            Socket &socket_;
            bool closed_{false};
            std::string size_str;
            template<typename T, size_t extent = unifex::dynamic_extent>
            friend auto tag_invoke(unifex::tag_t<net::async_send>, response_stream &stream, span<T, extent> data) {
                assert(!stream.closed_);
                constexpr auto discard = transform([](auto &&...) {});
                stream.size_str = fmt::format("{:x}\r\n", data.size());
                ;
                return sequence(net::async_send(stream.socket_,
                                                as_bytes(span{stream.size_str.data(), stream.size_str.size()}))
                                    | discard,
                                net::async_send(stream.socket_, data) | discard,
                                net::async_send(stream.socket_, as_bytes(span{"\r\n", 2})) | discard)
                     | transform([bytes = data.size()](auto &&...) { return bytes; });
            }
            friend auto tag_invoke(unifex::tag_t<net::async_send>, response_stream &stream) {
                stream.closed_ = true;
                return net::async_send(stream.socket_, as_bytes(span{"0\r\n\r\n", 5}));
            }
        };
        friend struct response_stream;

        friend auto tag_invoke(unifex::tag_t<net::async_send>, server_session &session, http::status status,
                               http::headers &&headers) {
            using namespace unifex;
            session.build_header(status, std::forward<http::headers>(headers));
            return net::async_send(session.socket_,
                                   as_bytes(span{session.header_data_.data(), session.header_data_.size()}))
                 | transform([&session](size_t) { return response_stream{session.socket_}; });
        }

        server_session(server_session &&other) noexcept
            : socket_{std::move(other.socket_)}, request_{socket_, buffer_} {}
        server_session(server_session const &other) = delete;

        struct request : detail::static_parser_handler<true> {
            Socket &socket_;
            session_buffer &buffer_;
            request(Socket &socket, session_buffer &buffer) noexcept : socket_{socket}, buffer_{buffer} {}

            request(request &&other) noexcept : socket_{other.socket_}, buffer_{other.buffer_} {};

            request(request const &other) = delete;

            friend auto tag_invoke(unifex::tag_t<net::async_recv>, request &request)
                -> unifex::any_sender_of<unifex::span<std::byte, unifex::dynamic_extent>> {
                using namespace unifex;
                if (request.has_body()) {
                    return just(request.body());
                } else {
                    return net::async_recv(request.socket_, as_writable_bytes(span{request.buffer_}))
                         | transform([&](size_t bytes) {
                               request.parse(span{request.buffer_.data(), bytes});
                               return request.body();
                           });
                }
            }
        };
    };

}// namespace g6::http
