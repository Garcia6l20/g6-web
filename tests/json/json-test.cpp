#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/json/json.hpp>

using namespace g6;

TEST_CASE("json loading", "[g6::web::json]") {//
    auto doc = json::load(std::string_view{R"({"value": 42})"});
    print("{}\n", doc);
    REQUIRE(doc.get<json::object>()["value"] == 42.0);
}