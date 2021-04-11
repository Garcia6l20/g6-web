#pragma once

#include <unifex/tag_invoke.hpp>

namespace g6 {
  constexpr struct make_server_cpo_ {
    template<typename Context, typename Proto, typename Endpoint>
    auto operator()(
        Context& ctx,
        const Proto& proto,
        Endpoint&& endpoint) const
    noexcept(unifex::is_nothrow_tag_invocable_v<
        make_server_cpo_, Context&,
        const Proto&, Endpoint&&>)
    -> unifex::tag_invoke_result_t<
        make_server_cpo_, Context&,
        const Proto&, Endpoint&&> {
      return unifex::tag_invoke(
          *this, ctx, proto, std::forward<Endpoint>(endpoint));
    }
  } make_server;

  constexpr struct async_serve_cpo_ {
    template<typename Server, typename ...Args>
    auto operator()(
        Server&& server,
        Args &&...args) const
    noexcept(unifex::is_nothrow_tag_invocable_v<
        async_serve_cpo_,
        Server,
        Args...>)
    -> unifex::tag_invoke_result_t<
        async_serve_cpo_,
        Server, Args...> {
      return unifex::tag_invoke(
          *this, std::forward<Server>(server), std::forward<Args>(args)...);
    }
  } async_serve;
}
