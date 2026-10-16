// Afterglow - A small JSON reader (see json.hpp).
// Copyright (C) 2026 DevvNirvana
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/json.hpp"

#include <cstdlib>

namespace hui::net
{

namespace
{

constexpr int kMaxDepth = 64;

const Json &null_value()
{
    static const Json kNull;
    return kNull;
}

const std::string &empty_string()
{
    static const std::string kEmpty;
    return kEmpty;
}

void append_utf8(std::string &out, unsigned code)
{
    if (code < 0x80)
    {
        out += static_cast<char>(code);
    }
    else if (code < 0x800)
    {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

class JsonParser
{
  public:
    explicit JsonParser(std::string_view text) : text_(text)
    {
    }

    bool document(Json *out)
    {
        if (!value(out, 0))
            return false;
        skip_space();
        return pos_ == text_.size();
    }

  private:
    void skip_space()
    {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(pos_, word.size()) != word)
            return false;
        pos_ += word.size();
        return true;
    }

    bool hex4(unsigned *code)
    {
        if (pos_ + 4 > text_.size())
            return false;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int d = hex_digit(text_[pos_ + static_cast<std::size_t>(i)]);
            if (d < 0)
                return false;
            value = value * 16u + static_cast<unsigned>(d);
        }
        pos_ += 4;
        *code = value;
        return true;
    }

    bool string(std::string *out)
    {
        if (pos_ >= text_.size() || text_[pos_] != '"')
            return false;
        ++pos_;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
            if (c != '\\')
            {
                *out += c;
                continue;
            }
            if (pos_ >= text_.size())
                return false;
            const char e = text_[pos_++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                *out += e;
                break;
            case 'b':
                *out += '\b';
                break;
            case 'f':
                *out += '\f';
                break;
            case 'n':
                *out += '\n';
                break;
            case 'r':
                *out += '\r';
                break;
            case 't':
                *out += '\t';
                break;
            case 'u':
            {
                unsigned code = 0;
                if (!hex4(&code))
                    return false;
                if (code >= 0xD800 && code < 0xDC00 && literal("\\u"))
                {
                    unsigned low = 0;
                    if (!hex4(&low) || low < 0xDC00 || low >= 0xE000)
                        return false;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(*out, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool number(double *out)
    {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        bool digits = false;
        while (pos_ < text_.size() &&
               ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' ||
                text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' ||
                text_[pos_] == '-'))
        {
            digits = digits || (text_[pos_] >= '0' && text_[pos_] <= '9');
            ++pos_;
        }
        if (!digits)
            return false;
        const std::string copy(text_.substr(start, pos_ - start));
        char *end = nullptr;
        *out = std::strtod(copy.c_str(), &end);
        return end == copy.c_str() + copy.size();
    }

    bool value(Json *out, int depth)
    {
        if (depth > kMaxDepth)
            return false;
        skip_space();
        if (pos_ >= text_.size())
            return false;
        const char c = text_[pos_];
        if (c == '{')
        {
            ++pos_;
            out->type_ = Json::Type::object;
            skip_space();
            if (pos_ < text_.size() && text_[pos_] == '}')
            {
                ++pos_;
                return true;
            }
            while (true)
            {
                skip_space();
                std::string key;
                if (!string(&key))
                    return false;
                skip_space();
                if (pos_ >= text_.size() || text_[pos_] != ':')
                    return false;
                ++pos_;
                Json member;
                if (!value(&member, depth + 1))
                    return false;
                out->members_.emplace_back(std::move(key), std::move(member));
                skip_space();
                if (pos_ < text_.size() && text_[pos_] == ',')
                {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == '}')
                {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[')
        {
            ++pos_;
            out->type_ = Json::Type::array;
            skip_space();
            if (pos_ < text_.size() && text_[pos_] == ']')
            {
                ++pos_;
                return true;
            }
            while (true)
            {
                Json item;
                if (!value(&item, depth + 1))
                    return false;
                out->items_.push_back(std::move(item));
                skip_space();
                if (pos_ < text_.size() && text_[pos_] == ',')
                {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == ']')
                {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"')
        {
            out->type_ = Json::Type::string;
            return string(&out->string_);
        }
        if (literal("true"))
        {
            out->type_ = Json::Type::boolean;
            out->boolean_ = true;
            return true;
        }
        if (literal("false"))
        {
            out->type_ = Json::Type::boolean;
            return true;
        }
        if (literal("null"))
            return true;
        out->type_ = Json::Type::number;
        return number(&out->number_);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

bool Json::parse(std::string_view text, Json *out)
{
    *out = Json();
    JsonParser parser(text);
    if (parser.document(out))
        return true;
    *out = Json();
    return false;
}

const Json &Json::operator[](std::string_view key) const
{
    if (type_ == Type::object)
        for (const auto &member : members_)
            if (member.first == key)
                return member.second;
    return null_value();
}

const Json &Json::at(std::size_t index) const
{
    if (type_ == Type::array && index < items_.size())
        return items_[index];
    return null_value();
}

const std::string &Json::str() const
{
    return type_ == Type::string ? string_ : empty_string();
}

} // namespace hui::net
