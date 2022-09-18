#include <g6/http/parser.hpp>

#include <catch2/catch.hpp>

#include <g6/logging.hpp>

using namespace g6;

TEST_CASE("http::headers", "[g6][web][http]") {
    SECTION("Accept-language") {
        http::headers m = {{"Accept-Language", "en"}, {"Hello", "world"}, {"hello", "guys"}};
        auto vals = m.values("accept-language");
        REQUIRE(vals.size() == 1);
        REQUIRE(vals[0] == "en");
    }
    SECTION("base") {
        http::headers m = {{"Hello", "world"}, {"hello", "guys"}};
        m.emplace("helLO", "girls");
        std::string_view f = "hello";
        std::string s{f};
        istring is{f};
        auto hellos = m.values(f);
        hellos = m.values(f);
        REQUIRE(hellos.size() == 3);
        REQUIRE(hellos[0] == "world");
        REQUIRE(hellos[1] == "guys");
        REQUIRE(hellos[2] == "girls");
        for (auto const &value : hellos) {
            spdlog::info("value: {}", value);
        }
    }
}

#include <random>

auto as_bytes(std::string_view data) noexcept {
    return std::span{reinterpret_cast<std::byte const*>(data.data()), data.size()};
}

/**
 * @brief Do parsing
 * 
 * Puts input into to given parser, randomly splitting input to check parser's robustness.
 * 
 * @param parser The http::parser<is_request> to use.
 * @param input The full input.
 */
std::tuple<bool, std::string> do_parse(auto &parser, std::string_view input) {
    static std::mt19937 rng_dev{size_t(::time(nullptr)) xor size_t(::getpid())};
    size_t offset = 0;
    bool res = false;
    std::string body;
    while (offset < input.size()) {
        size_t count = rng_dev() % (input.size() - offset);
        if (count < 1) {
            count = 1;
        }
        std::string_view this_input = input.substr(offset, count);
        spdlog::trace("do_parse ({}/{} bytes): {}", count, input.size() - offset, this_input);
        res = parser(as_bytes(this_input), [&body](auto in) {
            body.append(std::string_view{reinterpret_cast<const char*>(in.data()), in.size()});
        });
        offset += count;
    }
    return {res, std::move(body)};
}

// tool to help debugging non-working sequences
std::tuple<bool, std::string> do_parse_seq(auto &parser, std::string_view input, std::vector<size_t> steps) {
    size_t offset = 0;
    bool res = false;
    std::string body;
    for (size_t count : steps) {
        std::string_view this_input = input.substr(offset, count);
        spdlog::trace("do_parse ({}/{} bytes): {}", count, input.size() - offset, this_input);
        res = parser(as_bytes(this_input), [&body](auto in) {
            body.append(std::string_view{reinterpret_cast<const char*>(in.data()), in.size()});
        });
        offset += count;
    }
    return {res, std::move(body)};
}

TEST_CASE("http_parser requests", "[g6][web][http]") {
    spdlog::set_level(spdlog::level::trace);
    SECTION("basic/no content") {
        std::string_view data = "GET / HTTP/1.1\r\n"
                                "Host: test.org\r\n"
                                "Accept-Language: en\r\n"
                                "\r\n";
        http::request_parser p;
        REQUIRE(p(as_bytes(data), [](auto){
            FAIL("No body here !!!");
        }));
        REQUIRE(p.get_method() == http::method::get);
        REQUIRE(p.get_path() == "/");
        REQUIRE(p.get_protocol() == "HTTP/1.1");
        REQUIRE(p.get_headers("Host")[0] == "test.org");
        REQUIRE(p.get_headers("Accept-Language")[0] == "en");
    }


    SECTION("basic/with content") {
        std::string_view data = "GET / HTTP/1.1\r\n"
                                "Host: test.org\r\n"
                                "Accept-Language: en\r\n"
                                "Content-Length: 7\r\n"
                                "\r\n"
                                "Hello !";
        http::request_parser p;
        auto [done, body] = do_parse(p, data);
        // auto [done, body] = do_parse_seq(p, data, {34, 6, 3, 1, 8, 37}); // too much body data beeing parsed
        REQUIRE(done);
        REQUIRE(p.get_method() == http::method::get);
        REQUIRE(p.get_path() == "/");
        REQUIRE(p.get_protocol() == "HTTP/1.1");
        for (auto const& [field, value] : p.get_headers()) {
            spdlog::debug("field: {}, value: {}", field, value);
        }
        REQUIRE(p.get_headers("host")[0] == "test.org");
        REQUIRE(p.get_headers("accept-language")[0] == "en");
        REQUIRE(body == "Hello !");
    }
    SECTION("basic/chunked content") {
        std::string_view data = "GET / HTTP/1.1\r\n"
                                "Host: test.org\r\n"
                                "Accept-Language: en\r\n"
                                "Transfer-Encoding: chunked\r\n"
                                "\r\n"
                                "2\r\nHe\r\n"
                                "2\r\nll\r\n"
                                "3\r\no !\r\n"
                                "0\r\n\r\n";
        http::request_parser p;
        auto [done, body] = do_parse(p, data);
        // auto [done, body] = do_parse_seq(p, data, {1, 9, 1, 76, 12, 17});
        REQUIRE(done);
        REQUIRE(p.get_method() == http::method::get);
        REQUIRE(p.get_path() == "/");
        REQUIRE(p.get_protocol() == "HTTP/1.1");
        for (auto const& [field, value] : p.get_headers()) {
            spdlog::debug("field: {}, value: {}", field, value);
        }
        REQUIRE(p.get_headers("host")[0] == "test.org");
        REQUIRE(p.get_headers("accept-language")[0] == "en");
        REQUIRE(body == "Hello !");
    }
}

TEST_CASE("http_parser responses", "[g6][web][http]") {
    spdlog::set_level(spdlog::level::trace);
    SECTION("basic") {
        std::string_view data = "HTTP/1.1 200 OK\r\n"
                                "Host: test.org\r\n"
                                "Accept-Language: en\r\n"
                                "\r\n";
        http::response_parser p;
        REQUIRE(p(as_bytes(data), [](auto) {
            FAIL("No body here !!!");
        }));
        REQUIRE(p.get_protocol() == "HTTP/1.1");
        REQUIRE(p.get_status() == http::status::ok);
        REQUIRE(p.get_status_message() == "OK");
        REQUIRE(p.get_headers("Host")[0] == "test.org");
        REQUIRE(p.get_headers("Accept-Language")[0] == "en");
    }
}