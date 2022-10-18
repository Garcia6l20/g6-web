#pragma once


#include <g6/http/http.hpp>
#include <g6/format.hpp>
#include <g6/tl/crc32.hpp>

#include <bitset>
#include <charconv>
#include <functional>

namespace g6::http {

    template<bool is_request>
    class parser {

        static_assert(sizeof(std::string_view::value_type) == 1, "Char type must have 1-byte size !");

        using body_type = std::span<std::byte const, std::dynamic_extent>;
        using input_iterator = body_type::iterator;
        static constexpr inline auto npos = std::string_view::npos;
        using method_or_status = std::conditional_t<is_request, method, status>;

        method_or_status method_or_status_{};
        std::string path_or_smsg_;
        std::string proto_version_;
        headers headers_;
        std::optional<size_t> content_length_;
        size_t remaining_bytes_ = 0;
        enum class flag {
            has_content_length,
            is_chunked,
            count// sentinel
        };
        std::bitset<size_t(flag::count)> flags_;
        bool has(flag f) const noexcept { return flags_.test(size_t(f)); }
        void set(flag f) noexcept { flags_.set(size_t(f)); }
        bool needs_body() const noexcept { return has(flag::has_content_length) or has(flag::is_chunked); }

    public:
        static constexpr std::string_view eol = "\r\n";

        parser() = default;

        template <typename OnBodyFn, size_t extent = std::dynamic_extent>
        bool operator()(std::span<std::byte const, extent> input, OnBodyFn fn) {
            if (state_ == state::done) {
                return true;
            }
            input_iterator start = input.begin();
            input_iterator end = input.begin() + std::ptrdiff_t(input.size());
            if ((state_ == state::preamble) and (start[0] == std::byte{'\r'}) and (start[1] == std::byte{'\n'})) {
                start += 2; // RFC2616: 4.1
            }
            while (start != end) {
                std::invoke(ops_.at(size_t(state_)), this, start, end, [](body_type body, void *fn_ptr) {
                    static_cast<OnBodyFn*>(fn_ptr)->operator()(body);
                }, &fn);
            }
            return state_ == state::done;
        }

        method get_method() const noexcept requires(is_request) { return method_or_status_; }
        status get_status() const noexcept requires(not is_request) { return method_or_status_; }

        std::string const &get_path() const noexcept requires(is_request) { return path_or_smsg_; }
        std::string const &get_status_message() const noexcept requires(not is_request) { return path_or_smsg_; }

        std::string const &get_protocol() const noexcept { return proto_version_; }
        
        /**
         * @brief Get the headers map.
         * 
         * @return headers const& 
         */
        headers const& get_headers() const noexcept { return headers_; }

        /**
         * @brief Get the header values for given @a field.
         * 
         * @param field 
         * @return headers::header_values 
         */
        headers::header_values get_headers(std::string_view field) noexcept { return headers_.values(field); }

        /**
         * @brief Get the first header value matching @a field.
         * 
         * @param field 
         * @return std::optional<std::string_view> 
         */
        std::optional<std::string_view> get_header(std::string_view field) noexcept {
            istring field_istr = field;
            if (not headers_.contains(field_istr)) {
                return std::nullopt;
            }
            return headers_.values(field_istr)[0];
        }

        auto cookies() {
            std::map<std::string_view, std::string_view> result{};
            for (auto hdr : get_headers("Cookie")) {//
                for (auto [k, v] : hdr | std::views::split(';') | std::views::transform([](auto &&rng) {
                         auto kv = std::string_view{&*rng.begin(), size_t(std::ranges::distance(rng))}
                                 | std::views::split('=') | std::views::transform([](auto &&r) {
                                       return std::string_view{&*r.begin(), size_t(std::ranges::distance(r))};
                                   });
                         auto it = kv.begin();
                         return std::pair{*it, *(++it)};
                     })) {
                    result[trim(k)] = trim(v);
                }
            }
            return result;
        }

        operator bool() const noexcept {
            return state_ == state::done;
        }

    private:
        enum class state { preamble, header, body, done };
        state state_ = state::preamble;

        static status parse_status(std::string_view str);
        static method parse_method(std::string_view str);

        bool get_line(input_iterator &start, input_iterator const &end);

        using on_body_fn = void (*)(body_type body, void*);

        void do_preamble(input_iterator &start, input_iterator const &end, on_body_fn, void*);
        void do_header(input_iterator &start, input_iterator const &end, on_body_fn, void*);
        void do_body(input_iterator &start, input_iterator const &end, on_body_fn on_body, void*cb);

        using ops = void (parser<is_request>::*)(input_iterator &, input_iterator const &, on_body_fn, void*);
        std::array<ops, size_t(state::done)> ops_ = {&parser<is_request>::do_preamble, &parser<is_request>::do_header,
                                                     &parser<is_request>::do_body};
        std::string tmp_;
    };

    using request_parser = parser<true>;
    using response_parser = parser<false>;
}// namespace g6::http
