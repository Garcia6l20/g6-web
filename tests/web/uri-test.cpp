#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/web/uri.hpp>

using namespace g6;

TEST_CASE("g6::web::uri: address resolution", "[g6][web]") {
    web::uri uri{"http://google.com:80"};
    auto ep = uri.endpoint();
    REQUIRE(ep);
    spdlog::info("google endpoint: {}", ep->to_string());
}
