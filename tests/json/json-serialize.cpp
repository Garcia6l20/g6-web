#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/json/json.hpp>
#include <g6/poly/args.hpp>
#include <g6/to_tuple.hpp>

using namespace g6;

namespace g6::json {
    // template <poly::kwarg ...Args>
    // json::object serialize(std::tuple<Args...> &&args) {
    //     json::object result{};
    //     auto fn = [&](auto &&arg) {
    //         result.extend(std::forward<decltype(arg)>(arg));
    //     };
    //     [&]<size_t...I>(std::index_sequence<I...>) {
    //         (fn(std::get<I>(args)), ...);
    //     }(std::index_sequence_for<Args...>());
    //     return result;
    // }
    template<aggregate T>
    json::object serialize(T &&obj) {
        return json::object{to_tuple(std::forward<T>(obj))};
    }
}// namespace g6::json

struct test_struct_1 {
    poly::kw_arg<"test1", double> test1{1};
    poly::kw_arg<"test2", double> test2{2};
};
static_assert(aggregate<test_struct_1>);
static_assert(num_aggregate_unique_fields_v<test_struct_1> == 2);

TEST_CASE("json serilization", "[g6::web::json]") {//
    spdlog::set_level(spdlog::level::trace);
    SECTION("simple") {
        test_struct_1 test;
        auto test_tup = to_tuple(test);
        auto res = json::serialize(test);
        spdlog::debug("json::serialize(test) = {}", json::serialize(test));
    }
}