#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/json/json.hpp>

using namespace g6;

TEST_CASE("g6::web::json loading", "[g6][web][json]") {//
    SECTION("simple json dict") {
        auto dict = json::load(std::string_view{R"({"value":42})"});
        print("{}\n", dict);
        print("{}\n", json::dump(dict));
        REQUIRE(dict.get<json::object>()["value"] == 42.0);
        REQUIRE(json::dump(dict) == R"({"value":42})");
    }
    SECTION("complex json dict") {
        auto dict = json::load(std::string_view{R"({"value":42, "lst": [1, 42.2, "oops"], "none": null})"});
        print("{}\n", dict);
        print("{}\n", json::dump(dict));
        auto &o = dict.get<json::object>();
        REQUIRE(o["value"] == 42.0);
        auto &l = o["lst"].get<json::list>();
        print("{}\n", json::dump(l));
        REQUIRE(l[0] == 1.0);
        REQUIRE(l[1] == 42.2);
        REQUIRE(l[2] == "oops");
        REQUIRE(json::dump(dict) == R"({"value":42,"lst":[1,42.2,"oops"],"none":null})");
        REQUIRE(o["none"] == json::null);
    }
    SECTION("var interop") {
        using namespace std::string_literals;
        using namespace g6::poly::literals;
        print("{}\n", json::dump(poly::obj{"hello"_kw = "world"s, "value"_kw = 42}));
    }
    SECTION("syntax error") {
        REQUIRE_THROWS_AS(json::load(R"(\0)"), json::error);
        REQUIRE_THROWS_AS(json::load(R"(opps)"), json::error);
        REQUIRE_THROWS_AS(json::load(R"({opps})"), json::error);
        REQUIRE_THROWS_AS(json::load(R"({"value"="hum"})"), json::error);
        REQUIRE_THROWS_AS(json::load(R"({"value"42})"), json::error);
    }
}

TEST_CASE("g6::webjson: bug_fixes", "[g6][json][bug_fixes]") {
    SECTION("empty object") {
        auto result = json::load(R"([3,"79bO5qG8zczAET8hhd6t9f0qpeDmbnx7",{"0":{},"length":1}])").get<json::list>();
        REQUIRE(result.at(0) == 3.);
        REQUIRE(result.at(1) == "79bO5qG8zczAET8hhd6t9f0qpeDmbnx7");
        REQUIRE(result.at(2).is<json::object>());
        auto const& obj = result.at(2).get<json::object>();
        REQUIRE(obj.at("0").is<json::object>());
        REQUIRE(size(obj.at("0").get<json::object>()) == 0);
        REQUIRE(obj.at("length") == 1.);
    }
    SECTION("nested object") {
        auto result = json::load(R"([{"key":{"key":"bs.modal","id":1},"uidEvent":17}])");
    }
}
