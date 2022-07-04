#pragma once

#include <g6/poly/var.hpp>

#include <cstddef>
#include <cstdint>

#include <charconv>

namespace g6::json {

    using boolean = bool;
    using number = double;
    using string = std::string;

    using value = poly::var<boolean, number, string>;
    using list = poly::vec<boolean, number, string>;
    using object = poly::obj<boolean, number, string>;
    using null_t = poly::none;
    constexpr null_t null{};

    class error : public std::runtime_error {
    public:
        error(std::string const &what) noexcept : std::runtime_error{what} {}
    };

    template<typename IterT>
    constexpr IterT it_find(const IterT &begin, const IterT &end, char c) {
        auto ii = begin;
        for (; ii != end && *ii != c; ++ii) {}
        return ii;
    }

    template<typename IterT>
    value load(IterT &begin, const IterT &end);

    template<typename IterT>
    std::string parse_string(IterT &begin, const IterT &end);

    template<typename IterT>
    value parse_object(IterT &begin, const IterT &end) {
        object result;
        for (; begin != end && *begin != '}'; ++begin) {
            if (!isgraph(*begin)) continue;
            begin = it_find(begin, end, '"');
            if (begin == end) { throw error("syntax error"); }
            ++begin;
            std::string key = parse_string(begin, end);
            begin = it_find(begin, end, ':');
            if (begin == end) { throw error("syntax error"); }
            ++begin;
            result[std::move(key)] = load(begin, end);
            if (*begin == '}') break;
        }
        return value{std::move(result)};
    }

    template<typename IterT>
    value parse_array(IterT &begin, const IterT &end) {
        list result;
        for (; *begin != ']' && begin != end; ++begin) {
            if (!isgraph(*begin)) continue;
            if (*begin == ',') continue;
            result.push_back(load(begin, end));
        }
        return value{std::move(result)};
    }

    template<typename IterT>
    std::string parse_string(IterT &begin, const IterT &end) {
        std::string result;
        for (; begin != end; ++begin) {
            switch (*begin) {
                case '"':
                    return result;
                case '\\': {
                    // handle escape char
                    switch (*++begin) {
                        case 'n':
                            result += '\n';
                            break;
                        case '\\':
                            result += '\\';
                            break;
                        case '/':
                            result += '/';
                            break;
                        case 'b':
                            result += '\b';
                            break;
                        case 'f':
                            result += '\f';
                            break;
                        case 'r':
                            result += '\r';
                            break;
                        case 't':
                            result += '\t';
                            break;
                        case 'u':
                            throw std::runtime_error("Unicode not implemented yet !!");
                        default:
                            throw std::runtime_error(fmt::format("Unknown escape character: {}", *begin));
                    }
                    break;
                }
                default:
                    if (isprint(*begin)) result += *begin;
            }
        }
        throw std::runtime_error("Error occured while parsing string");
    }

    template<typename IterT>
    value parse_number(IterT &begin, const IterT &end) {
        thread_local static std::array<char, sizeof(double) * 2> tmp_chars;
        double result = 0.;
        auto [ptr, ec] = std::from_chars(&*begin, &*(end - 1), result);
        if (ec != std::errc{}) {
            throw error{format("failed to parse number: {}", std::make_error_code(ec).message())};
        }
        begin += ptr - &*begin;
        return value{result};
    }

    template<typename IterT>
    value load(IterT &begin, IterT const &end) {

        for (; begin != end; ++begin) {
            switch (*begin) {
                case ' ':
                case '\n':
                case '\t':
                    continue;
                case 't':// assuming true
                    begin += 3;
                    return true;
                case 'f':// assuming false
                    begin += 4;
                    return false;
                case 'n':// assuming null
                    begin += 3;
                    return null;
                case '{':
                    return parse_object(++begin, end);
                case '[':
                    return parse_array(++begin, end);
                case '"':
                    return value{parse_string(++begin, end)};
                default:
                    return parse_number(begin, end);
            }
        }
        throw std::runtime_error("Error occured while parsing value");
    }

    value load(std::string_view data) {//
        auto begin = std::begin(data);
        return load(begin, std::end(data));
    }

    template<typename T>
    std::string dump(T const &data) {
        thread_local static std::array<char, sizeof(double) * 2> tmp_chars;
        return poly::match(
            [&](null_t val) -> std::string {//
                return "null";
            },
            [&](boolean val) -> std::string {//
                return val ? "true" : "false";
            },
            [&](number val) -> std::string {//
                auto [ptr, ec] = std::to_chars(tmp_chars.data(), tmp_chars.data() + tmp_chars.size(), val);
                if (ec != std::errc{}) { throw std::system_error(std::make_error_code(ec)); }
                return {tmp_chars.data(), ptr};
            },
            [&](string const &val) -> std::string {//
                return '"' + val + '"';
            },
            [&]<typename... Values>(poly::obj<Values...> const &val)->std::string {//
                std::string result = "{";
                val | poly::for_each | [&, first = true]<typename Value>(string const &k, Value const &v) mutable {
                    if (not first) {
                        result += ',';
                    } else {
                        first = false;
                    }
                    result += '"' + k + "\":" + dump(v);
                };
                result += "}";
                return result;
            },
            // accept any kind of poly::vec, until the final value is convertible to number, string, bool or null
            [&]<typename... Values>(poly::vec<Values...> const &val)->std::string {//
                std::string result = "[";
                val | poly::for_each | [&, first = true]<typename Value>(Value const &v) mutable {
                    if (not first) {
                        result += ',';
                    } else {
                        first = false;
                    }
                    result += dump(v);
                };
                result += "]";
                return result;
            },
            // accept type convertible to number (ie.: int, float, etc...)
            [&]<typename U>(U v) -> std::string requires(
                                     std::is_arithmetic_v<U> and std::constructible_from<number, U>) {//
                return dump(static_cast<number>(v));
            },
            [](auto &&...) -> std::string {//
                throw std::runtime_error("invalid json value");
            })(data);
    }

    std::string dump(value const &data) {
        thread_local static std::array<char, sizeof(double) * 2> tmp_chars;
        return data | poly::visit | []<typename T>(T const &val) { return dump(val); };
    }

}// namespace g6::json
