#pragma once

#include <string>
#include <utility>
#include <variant>

namespace pf {

/// An error message meant for the user. Modeling operations return these
/// instead of throwing, so a rejected edit never reaches the document.
struct Error {
    std::string message;
};

/// A value or an Error (a small stand-in for C++23 std::expected).
template <class T>
class Expected {
public:
    Expected(T value) : data_(std::move(value)) {}            // NOLINT(google-explicit-constructor)
    Expected(Error error) : data_(std::move(error)) {}        // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }
    [[nodiscard]] const T& operator*() const& { return value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }

    [[nodiscard]] const std::string& error() const { return std::get<Error>(data_).message; }

private:
    std::variant<T, Error> data_;
};

} // namespace pf
