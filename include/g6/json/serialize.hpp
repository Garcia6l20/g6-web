#pragma once

#include <type_traits>

#include <g6/json/json.hpp>
#include <g6/poly/args.hpp>
#include <g6/to_tuple.hpp>
#include <g6/format.hpp>
#include <g6/from_string.hpp>

#include <stdexcept>
#include <optional>

namespace g6::json {

    struct missing_field_error : std::runtime_error {
        explicit missing_field_error(std::string_view field) noexcept
            : std::runtime_error{format("mandatory field is missing ({})", field)} {}
    };

    namespace details {

        template<typename T>
        concept arithmetic = std::is_arithmetic_v<T>;

        template<tl::aggregate T>
        auto make_object(T &&obj);

        template<typename T>
        auto load_value(T &&v) {
            return poly::match(
                // optionals
                // [&]<typename V>(std::optional<V> &&value) { //
                //     if (value) {
                //         return std::optional{value};
                //     }
                // },
                // arithmetic types
                [&]<arithmetic V>(V &&value) { return value; },
                // string
                [&]<std::convertible_to<json::string> V>(V &&value) { return json::string{std::move(value)}; },
                // nested object
                [&]<tl::aggregate V>(V &&value) { return make_object(std::move(value)); },
                // iterables
                [&]<std::ranges::range V>(V && value) requires(not std::convertible_to<V, json::string>) {
                    json::list l;
                    for (auto &&ii : std::move(value)) { l.emplace_back(load_value(std::move(ii))); }
                    return l;
                })(std::forward<T>(v));
        }

        template<tl::aggregate T>
        auto make_object(T &&obj) {
            json::object result;
            auto emplace_one = [&]<auto K, typename V>(poly::kw_arg<K, V> && value) {
                if constexpr (tl::specialization_of<V, std::optional>) {
                    if (value.value.has_value()) {
                        result.emplace(std::string{value.name}, load_value(std::move(value.value.value())));
                    }
                } else {
                    result.emplace(std::string{value.name}, load_value(std::move(value.value)));
                }
            };
            auto tup = tl::to_tuple(std::forward<T>(obj));
            [&]<size_t... I>(std::index_sequence<I...>) { (emplace_one(std::move(std::get<I>(tup))), ...); }
            (std::make_index_sequence<tl::num_aggregate_unique_fields_v<std::decay_t<T>>>());
            return result;
        }


        template<typename T, typename JsonT>
        T unload_value(const JsonT &data) {
            return data | poly::visit | []<typename V>(V const &val) -> T {
                if constexpr (std::same_as<V, null_t>) {//
                    return T{};
                } else if constexpr (std::same_as<V, T>) {//
                    return T{val};
                } else if constexpr (std::convertible_to<V, T>) {//
                    return T(val);
                } else if constexpr (tl::decays_to<V, object> and tl::aggregate<T>) {//
                    auto tup = tl::to_tuple(T{});
                    auto emplace_one = [&]<auto K, typename U>(poly::kw_arg<K, U> & value) {
                        std::string_view key = value.name;
                        if (val.contains(key)) { value = unload_value<U>(val.at(key)); }
                    };
                    [&]<size_t... I>(std::index_sequence<I...>) { (emplace_one(std::get<I>(tup)), ...); }
                    (std::make_index_sequence<tl::num_aggregate_unique_fields_v<std::decay_t<T>>>());
                    return std::make_from_tuple<T>(std::move(tup));
                } else if constexpr (tl::decays_to<V, list> and std::ranges::range<std::decay_t<T>>) {//
                    auto rng = T{};
                    rng.resize(val.size());
                    auto it = std::begin(rng);
                    for (auto const &item : val) {
                        *it = unload_value<typename T::value_type>(item);
                        ++it;
                    }
                    return rng;
                } else if constexpr (tl::decays_to<V, string> and from_stringable<T>) {
                    // load type from string
                    return from_string<T>(val).value();
                } else {
                    throw std::runtime_error(format("cannot match {} to {}", tl::type_name<JsonT>, tl::type_name<T>));
                }
            };
        }

        template<tl::aggregate T>
        auto unmake_object(json::object &&obj) {
            auto tup = tl::to_tuple(T{});
            auto emplace_one = [&]<auto K, typename V>(poly::kw_arg<K, V> & value) {
                const auto key = std::string_view{value.name};
                if (obj.contains(key)) {
                    value.value = unload_value<V>(std::move(obj.at(key)));
                } else if constexpr (not tl::specialization_of<V, std::optional>) {
                    throw missing_field_error{key};
                }
            };
            [&]<size_t... I>(std::index_sequence<I...>) { (emplace_one(std::get<I>(tup)), ...); }
            (std::make_index_sequence<tl::num_aggregate_unique_fields_v<std::decay_t<T>>>());
            return std::make_from_tuple<T>(std::move(tup));
        };
    }// namespace details

    template<tl::aggregate T>
    json::object serialize(T &&obj) {
        return details::make_object(std::forward<T>(obj));
    }

    template<tl::aggregate T>
    T deserialize(json::object &&obj) {
        return details::unmake_object<T>(std::forward<json::object>(obj));
    }
    template<tl::aggregate T>
    T deserialize(std::string_view data) {
        auto obj = load(data).get<object>();
        return deserialize<T>(std::move(obj));
    }
}// namespace g6::json

#define G6_JSON_FIELD(__type, __name) g6::poly::kw_arg<#__name, __type> __name
