#pragma once
/* express::Json = Json::Value (jsoncpp, already part of Drogon) + small helpers.

     res.json(express::obj("id", 7, "name", "Asha", "tags", express::arr("a", "b")));

   obj()/arr() take plain arguments instead of {{...}} braces, so they also compile inside a
   co_await expression on GCC 13 (same trick as nevla::obj / nevla::arr).
   Anything with a .toJson() member that returns a JSON string (nevla::Value, nevla documents, ...)
   can be passed straight in.                                                                       */

#include <json/json.h>

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace express {

using Json = Json::Value;

// "something that can print itself as JSON text", e.g. nevla::Value
template <class T>
concept HasToJson = requires(const T &t) {
    { t.toJson() } -> std::convertible_to<std::string>;
};

Json parseJson(std::string_view text);  // throws std::runtime_error on bad JSON
std::string stringify(const Json &v, bool pretty = false);

template <class T>
Json toJsonValue(T &&v) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, Json>)
        return v;
    else if constexpr (std::is_same_v<D, std::nullptr_t>)
        return Json(::Json::nullValue);
    else if constexpr (std::is_same_v<D, bool>)
        return Json(v);
    else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>)
        return Json(static_cast<::Json::Int64>(v));
    else if constexpr (std::is_integral_v<D>)
        return Json(static_cast<::Json::UInt64>(v));
    else if constexpr (std::is_floating_point_v<D>)
        return Json(static_cast<double>(v));
    else if constexpr (std::is_convertible_v<const D &, std::string_view>)
        return Json(std::string(std::string_view(v)));
    else if constexpr (HasToJson<D>)
        return parseJson(std::string(v.toJson()));
    else
        static_assert(sizeof(D) == 0, "Express++: this type cannot be converted to JSON");
}

namespace detail {
inline void objPut(Json &) {}
template <class K, class V, class... Rest>
void objPut(Json &o, K &&k, V &&v, Rest &&...rest) {
    o[std::string(std::string_view(k))] = toJsonValue(std::forward<V>(v));
    objPut(o, std::forward<Rest>(rest)...);
}
}  // namespace detail

// obj("a", 1, "b", "x")  ->  {"a": 1, "b": "x"}
template <class... A>
Json obj(A &&...kv) {
    static_assert(sizeof...(A) % 2 == 0, "express::obj needs key/value pairs");
    Json o(::Json::objectValue);
    detail::objPut(o, std::forward<A>(kv)...);
    return o;
}

// arr(1, "two", 3.0)  ->  [1, "two", 3.0]
template <class... A>
Json arr(A &&...items) {
    Json a(::Json::arrayValue);
    (a.append(toJsonValue(std::forward<A>(items))), ...);
    return a;
}

}  // namespace express
