#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/json/json.hpp>

using namespace g6;

TEST_CASE("json loading", "[g6::web::json]") {//
    SECTION("simple json dict") {
        auto dict = json::load(std::string_view{R"({"value":42})"});
        print("{}\n", dict);
        print("{}\n", json::dump(dict));
        REQUIRE(dict.get<json::object>()["value"] == 42.0);
        REQUIRE(json::dump(dict) == R"({"value":42})");
    }
    SECTION("complex json dict") {
        auto dict = json::load(std::string_view{R"({"value":42, "lst": [1, 42.2, "oops"]})"});
        print("{}\n", dict);
        print("{}\n", json::dump(dict));
        auto &o = dict.get<json::object>();
        REQUIRE(o["value"] == 42.0);
        auto &l = o["lst"].get<json::list>();
        print("{}\n", json::dump(l));
        REQUIRE(l[0] == 1.0);
        REQUIRE(l[1] == 42.2);
        REQUIRE(l[2] == "oops");
        REQUIRE(json::dump(dict) == R"({"value":42,"lst":[1,42.2,"oops"]})");
    }
    SECTION("Benchmarks") {
        BENCHMARK("deserialization") { return json::load(std::string_view{R"({"value":42,"lst":[1,42.2,"oops"]})"}); };
        auto data = json::load(std::string_view{R"({"value":42,"lst":[1,42.2,"oops"]})"});
        BENCHMARK("serialization") { return json::dump(data); };
    }
}