#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/json/serialize.hpp>

using namespace g6;

struct test_struct_1 {
    G6_JSON_FIELD(int, a){1};
    G6_JSON_FIELD(bool, b){true};
    G6_JSON_FIELD(float, c){42.2};
    G6_JSON_FIELD(std::string, d){"hello"};
};

struct test_struct_2 {
    G6_JSON_FIELD(int, num){1};
    G6_JSON_FIELD(test_struct_1, sub){};
};

struct test_struct_3 {
    G6_JSON_FIELD(std::vector<int>, arr){{1, 2, 3}};
};

struct test_struct_4 {
    G6_JSON_FIELD(std::vector<test_struct_2>, arr){{{1}, {2}, {3}}};
};

TEST_CASE("json serilization", "[g6::web::json]") {//
    spdlog::set_level(spdlog::level::trace);
    SECTION("serialize") {
        spdlog::debug("json::serialize(test) = {}", json::serialize(test_struct_1{42}));
        spdlog::debug("json::serialize(test) = {}", json::serialize(test_struct_2{42}));
        spdlog::debug("json::serialize(test) = {}", json::serialize(test_struct_3{}));
        spdlog::debug("json::serialize(test) = {}", json::serialize(test_struct_4{}));
    }
    SECTION("deserialize") {
        SECTION("simple") {
            test_struct_1 result = json::deserialize<test_struct_1>(json::serialize(test_struct_1{42, false, 43.3, "ok"}));
            REQUIRE(result.a == 42);
            REQUIRE(result.b == false);
            REQUIRE(result.c == 43.3f);
            REQUIRE(result.d == "ok");
        }
        SECTION("nested") {
            test_struct_2 result = json::deserialize<test_struct_2>(json::serialize(test_struct_2{42, {42, false, 43.3, "ok"}}));
            REQUIRE(result.num == 42);
            REQUIRE(result.sub->a == 42);
            REQUIRE(result.sub->b == false);
            REQUIRE(result.sub->c == 43.3f);
            REQUIRE(result.sub->d == "ok");
        }
        SECTION("array") {
            test_struct_3 result = json::deserialize<test_struct_3>(json::serialize(test_struct_3{{{42, 43, 44}}}));
            REQUIRE(result.arr->at(0) == 42);
            REQUIRE(result.arr->at(1) == 43);
            REQUIRE(result.arr->at(2) == 44);
        }
        SECTION("combined") {
            test_struct_4 result = json::deserialize<test_struct_4>(json::serialize(test_struct_4{{{{41}, {42}, {43}}}}));
            REQUIRE(result.arr->at(0).num == 41);
            REQUIRE(result.arr->at(1).num == 42);
            REQUIRE(result.arr->at(2).num == 43);
        }
    }
}

TEST_CASE("json serilization - special types", "[g6::web::json]") {//
    spdlog::set_level(spdlog::level::trace);
    SECTION("has_value") {
        struct test_opt {
            G6_JSON_FIELD(std::optional<double>, opt_dbl);
            G6_JSON_FIELD(double, man_dbl);
        };
        auto data = json::serialize(test_opt{
            .opt_dbl = 42.2,
            .man_dbl = 43.3
        });
        spdlog::debug("test_opt: {}", data);
        auto back = json::deserialize<test_opt>(std::move(data));
        REQUIRE(back.opt_dbl == 42.2);
        REQUIRE(back.man_dbl == 43.3);
    }
    SECTION("dont has_value") {
        struct test_opt {
            G6_JSON_FIELD(std::optional<double>, opt_dbl);
            G6_JSON_FIELD(double, man_dbl);
        };
        auto data = json::serialize(test_opt{
            .man_dbl = 43.3
        });
        spdlog::debug("test_opt: {}", data);
        auto back = json::deserialize<test_opt>(std::move(data));
        REQUIRE(not back.opt_dbl->has_value());
        REQUIRE(back.man_dbl == 43.3);
    }
    SECTION("missing field") {
        struct test_opt {
            G6_JSON_FIELD(std::optional<double>, opt_dbl);
            G6_JSON_FIELD(double, man_dbl);
        };
        REQUIRE_NOTHROW(json::deserialize<test_opt>(R"({"man_dbl":43.3})"));
        REQUIRE_THROWS_AS(json::deserialize<test_opt>(R"({"opt_dbl":43.3})"), json::missing_field_error);
    }
}