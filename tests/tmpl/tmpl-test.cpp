
#include <catch2/catch.hpp>

#include <g6/tmpl/tmpl.hpp>
#include <g6/tl/type_id.hpp>
#include <g6/tl/type_list.hpp>
#include <g6/tl/polymorphic_view.hpp>

#include <boost/hana.hpp>
#include <ctre.hpp>
#include <spdlog/spdlog.h>

#include <generator>
#include <ranges>
#include <variant>

namespace rng = std::ranges;
namespace hana = boost::hana;

using namespace g6;

#include <string_view>

class tmp
{
    struct text_block;
    struct control_block;
    struct render_block;
    struct for_block;

    using block_variant = std::variant<text_block, control_block, render_block, for_block>;
    using block_vector = tl::polymorphic_vector<text_block, control_block, render_block, for_block>;

    struct recursive_protector {
        static constexpr size_t max_depth = 5;
        size_t depth = 1;
        constexpr explicit recursive_protector() noexcept = default;
        constexpr recursive_protector(recursive_protector const &other) noexcept : depth{other.depth + 1} {}
        constexpr operator bool() const noexcept { return depth < max_depth; }
    };

    template <template <typename ...> typename Container, template <typename, typename> typename NamedArg, typename K, typename ...Vs>
    struct named_args_view : tl::polymorphic_view<Container, NamedArg<K, Vs>...>
    {
            constexpr named_args_view(Container<NamedArg<K, Vs>...> &container) noexcept : tl::polymorphic_view<Container, NamedArg<K, Vs>...>{container} {}

            decltype(auto) operator[](auto literal) {
                std::optional<tl::polymorphic_value<std::reference_wrapper<const Vs>...>> result;
                this->for_each([&result, &literal]<typename V>(NamedArg<K, V> &arg) mutable {
                    if (arg.name == literal.str) {
                        result.emplace(std::ref(arg.value));
                    }
                });
                if (not result) {
                    throw std::bad_variant_access();
                }
                return result.value();
            }
    };

    enum class token_type
    {
        open,
        close
    };

    template<typename Block>
    struct block_builder {
        using block_type = Block;
        std::string_view data;
        size_t open_pos = 0;
        size_t close_pos = 0;
        [[nodiscard]] std::string_view body() const noexcept {
            return data.substr(open_pos + 2, close_pos - open_pos - 2);
        }
        auto make() noexcept { return block_type{body()}; }
    };

    template<typename Concrete>
    struct base_block {
        Concrete &self() noexcept { return static_cast<Concrete &>(*this); }
        explicit base_block(std::string_view content, size_t parent_index = 0) noexcept
            : content_{content}, parent_index_{parent_index} {}
        std::string_view content_;
        size_t parent_index_;

        template<typename... Blocks>
        struct find_next_tag_result {
            std::variant<block_builder<Blocks>...> builder;
        };

        static constexpr auto find_first_of_all(auto &&r, auto &&needles) noexcept {
            constexpr auto npos = std::decay_t<decltype(r)>::npos;
            size_t first_pos = npos;
            for (auto &needle : needles) {
                if (auto it = r.find(needle); it != npos and it < first_pos) { first_pos = it; }
            }
            return first_pos;
        }

        template<typename... Blocks>
        constexpr static std::optional<find_next_tag_result<Blocks...>> find_next_tag(std::string_view data) {
            std::array open_tag = {Blocks::open_tag...};
            std::array close_tag = {Blocks::close_tag...};
            tl::polymorphic_array toks{std::tuple{Blocks::open_tag, block_builder<Blocks>{data}}...};
            std::optional<find_next_tag_result<Blocks...>> result;
            size_t open_pos = find_first_of_all(data, open_tag);
            size_t close_pos = find_first_of_all(data, close_tag);
            if (open_pos != std::string_view::npos && close_pos != std::string_view::npos) {
                rng::find_if(toks, [&](auto &tok_dat) {
                    return std::visit(
                        [&](auto &dat) {
                            size_t p = data.find(std::get<0>(dat));
                            auto &builder = std::get<1>(dat);
                            builder.open_pos = open_pos;
                            builder.close_pos = close_pos;
                            if (p == open_pos) { result.template emplace(std::move(builder)); }
                            return p == open_pos;
                        },
                        tok_dat);
                });
            }
            return result;
        }
    };

    struct text_block : base_block<text_block> {
        using base_block<text_block>::base_block;

        template<recursive_protector, typename... Args>
        void render(std::reference_wrapper<fmt::memory_buffer> buffer, hana::tuple<Args...> &args) const {
            fmt::format_to(buffer.get(), content_);
        }
    };

    struct render_block : base_block<render_block> {

        static constexpr std::string_view open_tag = "{{";
        static constexpr std::string_view close_tag = "}}";

        std::string_view stripped_content;

        static constexpr auto strip(std::string_view view) noexcept {
            view.remove_prefix(std::min(view.find_first_not_of(" "), view.size()));
            view.remove_suffix(view.size() - view.find_last_not_of(" ") - 1);
            return view;
        }

        explicit render_block(std::string_view content, size_t parent_index = 0) noexcept
            : base_block<render_block>::base_block{content, parent_index} {
            stripped_content = strip(content);
            stripped_content.remove_prefix(std::min(stripped_content.find_first_not_of(" "), stripped_content.size()));
            stripped_content.remove_suffix(stripped_content.size() - stripped_content.find_last_not_of(" ") - 1);
        }

        template<recursive_protector RP, typename... Args>
        void render(std::reference_wrapper<fmt::memory_buffer> buffer, hana::tuple<Args...> &args) const {
            hana::for_each(args, [&]<typename Char, typename T>(fmt::detail::named_arg<Char, T> &arg) {
                if constexpr (requires { fmt::make_args_checked(arg.value); }) {
                    auto name = strip(arg.name);
                    if(auto space_it = name.find(" "); space_it != std::string_view::npos) {
                        name.remove_suffix(name.size() - space_it);
                    }
                    if (name == stripped_content) {
                        fmt::format_to(buffer.get(), arg.value);
                    }
                }
            });
        }
    };

    struct control_block : base_block<render_block> {
        using base_block<render_block>::base_block;
        block_vector children_{};

        static constexpr std::string_view open_tag = "{%";
        static constexpr std::string_view close_tag = "%}";

        auto get_current(size_t index) const noexcept { return content_.substr(index, content_.size() - index); }

        std::generator<block_variant> generate_children() {
            size_t index = 0;
            while (index < content_.size()) {
                if (auto tag = find_next_tag<control_block, render_block>(get_current(index)); tag) {
                    auto sub = std::visit(
                        [&]<typename Block>(block_builder<Block> &builder) -> std::generator<block_variant> {
                            return [](control_block &self, size_t &index,
                                      block_builder<Block> builder) -> std::generator<block_variant> {
                                auto remaining_text = self.get_current(index);
                                remaining_text.remove_suffix(remaining_text.size() - builder.open_pos);
                                if (not remaining_text.empty()) {
                                    spdlog::info("remaining_text: {} {} {} {}", index, builder.open_pos,
                                                 builder.close_pos, remaining_text);
                                    co_yield text_block{remaining_text};
                                }
                                if constexpr (std::same_as<render_block, Block>) {
                                    co_yield builder.template make();
                                    index += builder.close_pos + 2;
                                    spdlog::info("render: {}", self.get_current(index));
                                } else {// std::same_as<control_block, Block>
                                    auto start_block = builder.template make();
                                    //                                    spdlog::info("{}", start_block.content_);
                                    index += builder.close_pos + 2;
                                    spdlog::info("control: {}", self.get_current(index));
                                    if (auto [m, lhs, rhs] =
                                            ctre::match<R"(\s?for\s+(\w+)\s+in\s+(\w+)\s+)">(start_block.content_);
                                        m) {
                                        auto blk = for_block{lhs, rhs, self.get_current(index)};
                                        index += blk.content_.size();
                                        spdlog::info("for: {} ({})", self.get_current(index), blk.content_);
                                        co_yield std::move(blk);
                                    } else if (auto m = ctre::match<R"(\s?endfor\s?)">(start_block.content_); m) {
                                        self.content_.remove_suffix(self.content_.size() - index);
                                        spdlog::info("endfor: {}", self.content_);
                                        co_return;
                                    } else {
                                        throw std::runtime_error{
                                            fmt::format("unknown block type: {}", start_block.content_)};
                                    }
                                }
                            }(*this, std::ref(index), std::move(builder));
                        },
                        tag->builder);
                    for (auto &&it : sub) { co_yield std::forward<decltype(it)>(it); }
                } else {
                    auto remaining_text = get_current(index);
                    if (not remaining_text.empty()) {
                        spdlog::info("remaining_text: {}", remaining_text);
                        co_yield text_block{remaining_text};
                    }
                    break;
                }
            }
        }

        void parse() {
            for (auto blk : generate_children()) { children_.emplace_back(std::move(blk)); }
        }

        template<recursive_protector RP, typename... Args>
        void render(std::reference_wrapper<fmt::memory_buffer> buffer, hana::tuple<Args...> &args) const {
            if constexpr (RP) {
                for (auto &child : children_) {
                    std::visit([&]<typename Block>(Block &block) { block.template render<RP>(buffer.get(), args); },
                               child);
                }
            }
        }
    };

    struct for_block : control_block {
        std::string_view lhs_;
        std::string_view rhs_;
        for_block(std::string_view lhs, std::string_view rhs, std::string_view content) noexcept
            : lhs_{lhs}, rhs_{rhs}, control_block{content} {
            spdlog::info("for block: {} {} {}", lhs, rhs, content);
            parse();
        }

        template<recursive_protector RP, typename... Args>
        void render(std::reference_wrapper<fmt::memory_buffer> buffer, hana::tuple<Args...> &args) const {
            if constexpr (RP) {
                using args_list = typename tl::type_list<decltype(Args::value)...>;
                using iterable_args = typename args_list::transform<[]<typename T>(T value) {
                    if constexpr (requires { begin(value); end(value); }) {
                        return std::reference_wrapper<const T>{value};
                    }
                }>;
                
                using args_value_tuple = typename iterable_args::as<hana::tuple>;
                using args_value_variant = typename iterable_args::as<std::variant>;

                std::optional<args_value_variant> rhs{};

                // named_args_view v{args};
                // args[rhs]([&]<typename T>(T const& value) {

                // });

                hana::for_each(args, [&]<typename Char, typename T>(fmt::detail::named_arg<Char, T> &arg) {
                    if (arg.name == rhs_) {
                        if constexpr (std::is_constructible_v<args_value_variant, std::reference_wrapper<const T>>) {
                            rhs = arg.value;
                        }
                        else
                        {
                            throw std::runtime_error{fmt::format("argument is not iterable: {}", rhs_)};
                        }
                    }
                });
                if (rhs) {
                    size_t ii = 0;
                    std::visit(
                        [&]<typename Rhs>(Rhs const &rhs) {
                            for (auto it = begin(rhs.get()); it != end(rhs.get()); ++it) {
                                auto &lhs = *it;
                                auto new_args = hana::append(args, fmt::arg(lhs_.data(), lhs));
                                for (auto &child : children_) {
                                    std::visit([&]<typename Block>(
                                                    Block &block) { block.template render<RP>(buffer, new_args); },
                                                child);
                                }
                                ++ii;
                            }
                        },
                        *rhs);
                } else {
                    throw std::runtime_error{fmt::format("cannot find argument: {}", rhs_)};
                }
            }
        }
    };

    control_block root_;

public:
    explicit tmp(std::string_view input) : root_{input} { root_.parse(); }

    std::string operator()(auto &&...args_v) const {
        fmt::memory_buffer buffer{};
        auto args = hana::make_tuple(std::forward<decltype(args_v)>(args_v)...);
        root_.render<recursive_protector{}>(buffer, args);
        return to_string(buffer);
    }
};


using namespace std::literals;
using namespace fmt::literals;

TEST_CASE("simple rendering test", "[g6::web::tmpl]") {
    spdlog::set_level(spdlog::level::debug);
    REQUIRE(tmp{"Hello {{ who }} !"}("who"_a = "world"sv) == "Hello world !");
    REQUIRE(tmp{"Hello {{who}} !"}("who"_a = "world"sv) == "Hello world !");
}

TEST_CASE("simple for test", "[g6::web::tmpl]") {
    spdlog::set_level(spdlog::level::debug);
    REQUIRE(tmp{R"(Hello {% for who in whose %}{{ who }}, {% endfor %}!)"}("whose"_a = std::array{"world"sv, "me"sv})
            == "Hello world, me, !");
    // REQUIRE(tmp{R"(Hello {% for who in whose %}{{ who }} {% endfor %}!)"}("whose"_a = "world"sv)
    //         == "Hello w o r l d !");
}