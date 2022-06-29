#include <g6/http/impl/static_parser_handler.hpp>

#include <spdlog/spdlog.h>

namespace g6::http::detail {
#include <http_parser.h>

    template<bool is_request>
    static_parser_handler<is_request>::static_parser_handler() noexcept {
        c_parser_ = new detail::http_parser;
        http_parser_init(static_cast<detail::http_parser*>(c_parser_), is_request ? http_parser_type::HTTP_REQUEST : http_parser_type::HTTP_RESPONSE);
        static_cast<detail::http_parser*>(c_parser_)->data = this;
    }

    template<bool is_request>
    static_parser_handler<is_request>::~static_parser_handler() noexcept {
        if (c_parser_)
            delete static_cast<detail::http_parser*>(c_parser_);
        c_parser_ = nullptr;
    }
    
    template<bool is_request>
    static_parser_handler<is_request>::static_parser_handler(static_parser_handler &&other) noexcept
        : c_parser_{std::exchange(other.c_parser_, nullptr)},
          header_field_{std::move(other.header_field_)}, url_{std::move(other.url_)}, body_{std::move(other.body_)},
          state_{std::move(other.state_)}, headers_{std::move(other.headers_)} {
        static_cast<detail::http_parser*>(c_parser_)->data = this;
    }

    template<bool is_request>
    static_parser_handler<is_request> &
    static_parser_handler<is_request>::operator=(static_parser_handler &&other) noexcept {
        c_parser_ = std::exchange(other.c_parser_, nullptr);
        header_field_ = std::move(other.header_field_);
        url_ = std::move(other.url_);
        body_ = std::move(other.body_);
        state_ = std::move(other.state_);
        headers_ = std::move(other.headers_);
        static_cast<detail::http_parser*>(c_parser_)->data = this;
        return *this;
    }

    template<bool is_request>
    bool static_parser_handler<is_request>::parse(std::span<std::byte const> data) {
        body_ = {};
        spdlog::debug("static_parser_handler::parse: {}",
                      std::string_view{reinterpret_cast<const char *>(data.data()), data.size()});
        const auto count = execute_parser(reinterpret_cast<const char *>(data.data()), data.size());
        if (count < data.size()) {
            throw std::runtime_error{fmt::format(
                FMT_STRING("parse error: {}"),
                http_errno_description(detail::http_errno(static_cast<http_parser*>(c_parser_)->http_errno)))};
        }
        return state_ == parser_status::on_message_complete;
    }

    template<bool is_request>
    [[nodiscard]] bool static_parser_handler<is_request>::chunked() const {
        if (static_cast<detail::http_parser*>(c_parser_)->uses_transfer_encoding) {
            return header_at("Transfer-Encoding") == std::string_view{"chunked"};
        } else {
            return false;
        }
    }

    template<bool is_request>
    http::method static_parser_handler<is_request>::method() const {
        return http::method(static_cast<detail::http_parser*>(c_parser_)->method);
    }

    template<bool is_request>
    http::status static_parser_handler<is_request>::status_code() const {
        return http::status(static_cast<detail::http_parser*>(c_parser_)->status_code);
    }

    template<bool is_request>
    std::string static_parser_handler<is_request>::to_string() const {
        std::string out;
        std::string_view type;
        if constexpr (is_request) {
            fmt::format_to(std::back_inserter(out), "request {} {}", http::to_string(method()), url_);
        } else {
            fmt::format_to(std::back_inserter(out), "response {}", http::to_string(status_code()));
        }
        fmt::format_to(std::back_inserter(out), "{}", std::string_view{reinterpret_cast<const char*>(body_.data()), body_.size()});
        return out;
    }

    template<bool is_request>
    inline static auto &instance(void *parser) {
        return *static_cast<static_parser_handler<is_request> *>(static_cast<detail::http_parser*>(parser)->data);
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_message_begin(void *parser) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_message_begin;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_url(void *parser, const char *data, size_t len) {
        auto &this_ = instance<is_request>(parser);
        this_.url_ = web::uri::unescape({data, len});
        this_.state_ = parser_status::on_url;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_status(void *parser, const char *data, size_t len) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_status;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_header_field(void *parser, const char *data, size_t len) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_header_field;
        this_.header_field_ = {data, len};
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_header_value(void *parser, const char *data, size_t len) {
        auto &this_ = instance<is_request>(parser);
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

    template<bool is_request>
    int static_parser_handler<is_request>::on_headers_complete(void *parser) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_headers_complete;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_body(void *parser, const char *data, size_t len) {
        auto &this_ = instance<is_request>(parser);
        this_.body_ = std::as_writable_bytes(std::span{const_cast<char *>(data), len});
        this_.state_ = parser_status::on_body;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_message_complete(void *parser) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_message_complete;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_chunk_header(void *parser) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_chunk_header;
        return 0;
    }

    template<bool is_request>
    int static_parser_handler<is_request>::on_chunk_complete(void *parser) {
        auto &this_ = instance<is_request>(parser);
        this_.state_ = parser_status::on_chunk_header_compete;
        return 0;
    }

    template<bool is_request>
    size_t static_parser_handler<is_request>::execute_parser(const char *data, size_t len) {
        const static detail::http_parser_settings settings = {
            (detail::http_cb) on_message_begin,
            (detail::http_data_cb) on_url,
            (detail::http_data_cb) on_status,
            (detail::http_data_cb) on_header_field,
            (detail::http_data_cb) on_header_value,
            (detail::http_cb) on_headers_complete,
            (detail::http_data_cb) on_body,
            (detail::http_cb) on_message_complete,
            (detail::http_cb) on_chunk_header,
            (detail::http_cb) on_chunk_complete,
        };
        return http_parser_execute(static_cast<detail::http_parser*>(c_parser_), &settings, data, len);
    }

    template class static_parser_handler<true>;
    template class static_parser_handler<false>;

}// namespace g6::http::detail
