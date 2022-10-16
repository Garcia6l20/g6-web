#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch.hpp>

#include <g6/json/json.hpp>

using namespace g6;

TEST_CASE("g6::web::json loading benchmarks", "[g6][web][json]") {//
    BENCHMARK("deserialization") { return json::load(std::string_view{R"({"value":42,"lst":[1,42.2,"oops"]})"}); };
    auto data = json::load(std::string_view{R"({"value":42,"lst":[1,42.2,"oops"]})"});
    BENCHMARK("serialization") { return json::dump(data); };
}