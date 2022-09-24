#include <g6/http/parser.hpp>

namespace g6::http {

    template<bool is_request>
    status parser<is_request>::parse_status(std::string_view str) {
        std::uint16_t status_value = 0;
        auto [_, ec] = std::from_chars(str.begin(), str.end(), status_value);
        switch (status_value) {
#define XX(num, name, string) case num:
                G6_HTTP_STATUS_MAP(XX)
#undef XX
            return status(status_value);
            default:
                throw std::runtime_error(format("cannot parse status: {}", str));
        }
    }

    template<bool is_request>
    method parser<is_request>::parse_method(std::string_view str) {
        switch (uint32_t(tl::crc32(str))) {
#define XX(num, name, string)                                                                                          \
    case uint32_t(tl::crc32(#string)):                                                                                 \
        return method::name;
                G6_HTTP_METHOD_MAP(XX)
#undef XX
            default:
                throw std::runtime_error(format("cannot parse method: {}", str));
        }
    }

    template<bool is_request>
    bool parser<is_request>::get_line(input_iterator &start, input_iterator const &end) {
        auto it = std::find_first_of(start, end, reinterpret_cast<const std::byte *>(eol.begin()),
                                     reinterpret_cast<const std::byte *>(eol.end()));
        if (it != end) {
            if ((it + 1 != end) and *(it + 1) == std::byte{'\n'}) {
                it += 2;
            } else {
                ++it;
            }
        }
        tmp_.append(reinterpret_cast<const char *>(&start[0]), reinterpret_cast<const char *>(&it[0]));
        start = it;
        return tmp_.ends_with(eol);
    }

    template<bool is_request>
    void parser<is_request>::do_preamble(input_iterator &start, input_iterator const &end, on_body_fn, void *) {
        if (get_line(start, end)) {
            auto wip = std::string_view{tmp_.data(), tmp_.data() + tmp_.size()};
            auto pos = wip.find(' ');
            if (pos == npos) { goto __err; }
            if constexpr (is_request) {
                method_or_status_ = parse_method(wip.substr(0, pos));
            } else {
                proto_version_ = wip.substr(0, pos);
            }
            wip.remove_prefix(pos + 1);
            pos = wip.find(' ');
            if (pos == npos) { goto __err; }
            if constexpr (is_request) {
                path_or_smsg_ = wip.substr(0, pos);
            } else {
                method_or_status_ = parse_status(wip.substr(0, pos));
            }
            wip.remove_prefix(pos + 1);
            pos = wip.find(eol);
            if (pos == npos) { goto __err; }
            if constexpr (is_request) {
                proto_version_ = wip.substr(0, pos);
            } else {
                path_or_smsg_ = wip.substr(0, pos);
            }
            tmp_.clear();
            state_ = state::header;
        }
        return;
    __err : { throw std::runtime_error("Cannot parse http preamble"); }
    }

    template<bool is_request>
    void parser<is_request>::do_header(input_iterator &start, input_iterator const &end, on_body_fn, void*) {
        if (get_line(start, end)) {

            if (tmp_ == eol) {
                tmp_.clear();
                state_ = needs_body() ? state::body : state::done;
                return;
            }

            auto wip = std::string_view{tmp_.data(), tmp_.data() + tmp_.size()};
            auto pos = wip.find(":");
            if (pos == npos) {//
                goto __err;
            }
            auto field = trim(wip.substr(0, pos));
            wip.remove_prefix(pos + 1);
            pos = wip.find(eol);
            if (pos == npos) {//
                goto __err;
            }
            auto value = trim(wip.substr(0, pos));

            // special headers
            if (iequals(field, "content-length")) {
                size_t content_length = 0;
                auto [_, ec] = std::from_chars(value.begin(), value.end(), content_length);
                if (ec == std::errc{}) {
                    content_length_ = content_length;
                    remaining_bytes_ = content_length;
                    if (content_length) {
                        set(flag::has_content_length);
                    }
                } else {
                    throw std::system_error{std::make_error_code(ec)};
                }
            }
            if (iequals(field, "transfer-encoding")) { set(flag::is_chunked); }

            headers_.emplace(field, value);
            tmp_.clear();
        }
        return;
    __err : { throw std::runtime_error("Cannot parse http header"); }
    }

    template<bool is_request>
    void parser<is_request>::do_body(input_iterator &start, input_iterator const &end, on_body_fn on_body, void*cb) {
        if (has(flag::is_chunked)) {
            if (remaining_bytes_) {
                // get previous uncomplete chunk
                const size_t bytes = std::min(std::distance(start, end), ssize_t(remaining_bytes_));
                remaining_bytes_ -= bytes;
                on_body({start, start + bytes}, cb);
                start += bytes;
            } else if (get_line(start, end)) {

                if (tmp_ == eol) {
                    tmp_.clear();
                    return;
                }
                
                uint64_t chunk_size = 0;
                auto [_, ec] = std::from_chars(tmp_.data(), tmp_.data() + tmp_.size() - 2u, chunk_size, 16);
                tmp_.clear();
                if (ec != std::errc{}) {
                    throw std::system_error{std::make_error_code(ec)};
                }
                if (chunk_size == 0) {
                    start = end;
                    state_ = state::done;
                    return;
                } else if ((start + chunk_size) < end) {
                    // chunk full available
                    on_body({start, start + chunk_size}, cb);
                    start += chunk_size;
                } else {
                    // chunk uncomplete
                    const auto bytes = std::distance(start, end);
                    if (bytes > 0) {
                        remaining_bytes_ = chunk_size - bytes;
                        on_body({start, end}, cb);
                        start = end;
                    } else {
                        remaining_bytes_ = chunk_size;
                    }
                }
            }
        } else if(content_length_.has_value()) {
            auto byte_count = std::distance(start, end);
            if (byte_count > remaining_bytes_) {
                throw std::runtime_error("too much body data beeing parsed");
            }
            remaining_bytes_ -= byte_count;
            on_body({start, end}, cb);
            if (remaining_bytes_ == 0) {
                state_ = state::done;
            }
            start = end;
        } else {
            throw std::runtime_error("unhandled body transfert");
        }
    }

    template class parser<true>;
    template class parser<false>;
};// namespace g6::http
