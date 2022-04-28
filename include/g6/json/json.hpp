#include <g6/tag_invoke>

#include <g6/poly/var.hpp>

#include <cstddef>
#include <cstdint>

namespace g6::json {

    using boolean = bool;
    using number = double;
    using string = std::string;

    using value = poly::var<boolean, number, string>;
    using list = poly::vec<boolean, number, string>;
    using object = poly::obj<boolean, number, string>;
    using none = poly::none;

    template<typename IterT>
    inline IterT it_find(const IterT &begin, const IterT &end, char c) {
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
            begin = it_find(begin, end, '"') + 1;
            std::string key = parse_string(begin, end);
            begin = it_find(begin, end, ':') + 1;
            result[std::move(key)] = load(begin, end);
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
        double result = 0.;
        size_t sz = 0;
        result = std::stod(std::string(begin, end), &sz);
        if (sz == 0) throw std::runtime_error("Error occured while parsing number");
        begin += sz - 1;
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
                    return none{};
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

}// namespace g6::json
