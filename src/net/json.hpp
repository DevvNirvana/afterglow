// Afterglow - A small JSON reader, enough for web API answers.
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parses a document into a tree of values. No exceptions (the console build
// has none): parse() reports failure through its return value. Numbers are
// doubles; \u escapes become UTF-8 (surrogate pairs included); nesting is
// limited so a hostile answer cannot exhaust the stack.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui::net
{

class Json
{
  public:
    enum class Type : unsigned char
    {
        null,
        boolean,
        number,
        string,
        array,
        object,
    };

    // False (and *out a null) if text is not one complete JSON value.
    static bool parse(std::string_view text, Json *out);

    Type type() const
    {
        return type_;
    }
    bool is_object() const
    {
        return type_ == Type::object;
    }
    bool is_array() const
    {
        return type_ == Type::array;
    }
    // The member of an object called key, or a shared null value.
    const Json &operator[](std::string_view key) const;
    // An array's element, or a shared null value.
    const Json &at(std::size_t index) const;
    std::size_t size() const
    {
        return type_ == Type::array ? items_.size() : type_ == Type::object ? members_.size() : 0;
    }
    const std::vector<Json> &items() const
    {
        return items_;
    }
    // The value as a string / number / bool, or fallback when it is another type.
    const std::string &str() const;
    double number(double fallback = 0.0) const
    {
        return type_ == Type::number ? number_ : fallback;
    }
    bool boolean(bool fallback = false) const
    {
        return type_ == Type::boolean ? boolean_ : fallback;
    }

  private:
    friend class JsonParser;
    Type type_ = Type::null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};

} // namespace hui::net
