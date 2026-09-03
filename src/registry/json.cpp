#include "registry/json.hpp"

#include <cctype>
#include <charconv>
#include <limits>
#include <utility>

bool JsonValue::is_null() const noexcept {
  return std::holds_alternative<std::nullptr_t>(value_);
}

const bool *JsonValue::boolean() const noexcept {
  return std::get_if<bool>(&value_);
}

const std::int64_t *JsonValue::integer() const noexcept {
  return std::get_if<std::int64_t>(&value_);
}

const std::string *JsonValue::string() const noexcept {
  return std::get_if<std::string>(&value_);
}

const JsonValue::Array *JsonValue::array() const noexcept {
  return std::get_if<Array>(&value_);
}

const JsonValue::Object *JsonValue::object() const noexcept {
  return std::get_if<Object>(&value_);
}

namespace {

class Parser {
public:
  explicit Parser(std::string_view source) : source_(source) {}

  JsonParseResult parse() {
    skip_space();
    auto result = parse_value(0);
    if (!result)
      return result;
    skip_space();
    if (position_ != source_.size())
      return failure("Unexpected content after the JSON document");
    return result;
  }

private:
  JsonParseResult parse_value(unsigned depth) {
    if (depth > 64)
      return failure("JSON nesting is too deep");
    if (position_ >= source_.size())
      return failure("JSON document ended unexpectedly");
    switch (source_[position_]) {
    case '{':
      return parse_object(depth + 1);
    case '[':
      return parse_array(depth + 1);
    case '"': {
      std::string value;
      if (!parse_string(value))
        return failure(error_);
      return {JsonValue(std::move(value)), {}};
    }
    case 't':
      return parse_literal("true", JsonValue(true));
    case 'f':
      return parse_literal("false", JsonValue(false));
    case 'n':
      return parse_literal("null", JsonValue());
    default:
      if (source_[position_] == '-' ||
          std::isdigit(static_cast<unsigned char>(source_[position_])))
        return parse_integer();
      return failure("Invalid JSON value");
    }
  }

  JsonParseResult parse_object(unsigned depth) {
    ++position_;
    skip_space();
    JsonValue::Object object;
    if (consume('}'))
      return {JsonValue(std::move(object)), {}};
    while (position_ < source_.size()) {
      std::string key;
      if (!parse_string(key))
        return failure(error_);
      skip_space();
      if (!consume(':'))
        return failure("Missing colon after JSON object key");
      skip_space();
      auto value = parse_value(depth);
      if (!value)
        return value;
      if (!object.emplace(std::move(key), std::move(value.value)).second)
        return failure("JSON object contains a duplicate key");
      skip_space();
      if (consume('}'))
        return {JsonValue(std::move(object)), {}};
      if (!consume(','))
        return failure("Missing comma between JSON object members");
      skip_space();
    }
    return failure("JSON object ended unexpectedly");
  }

  JsonParseResult parse_array(unsigned depth) {
    ++position_;
    skip_space();
    JsonValue::Array array;
    if (consume(']'))
      return {JsonValue(std::move(array)), {}};
    while (position_ < source_.size()) {
      auto value = parse_value(depth);
      if (!value)
        return value;
      array.push_back(std::move(value.value));
      skip_space();
      if (consume(']'))
        return {JsonValue(std::move(array)), {}};
      if (!consume(','))
        return failure("Missing comma between JSON array elements");
      skip_space();
    }
    return failure("JSON array ended unexpectedly");
  }

  JsonParseResult parse_integer() {
    const auto start = position_;
    if (source_[position_] == '-')
      ++position_;
    if (position_ >= source_.size())
      return failure("Invalid JSON number");
    if (source_[position_] == '0') {
      ++position_;
      if (position_ < source_.size() &&
          std::isdigit(static_cast<unsigned char>(source_[position_])))
        return failure("JSON number contains a leading zero");
    } else {
      if (!std::isdigit(static_cast<unsigned char>(source_[position_])))
        return failure("Invalid JSON number");
      while (position_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_])))
        ++position_;
    }
    if (position_ < source_.size() &&
        (source_[position_] == '.' || source_[position_] == 'e' ||
         source_[position_] == 'E'))
      return failure("Registry JSON does not accept non-integer numbers");
    std::int64_t value = 0;
    const auto token = source_.substr(start, position_ - start);
    const auto converted =
        std::from_chars(token.data(), token.data() + token.size(), value);
    if (converted.ec != std::errc{} ||
        converted.ptr != token.data() + token.size())
      return failure("JSON integer is out of range");
    return {JsonValue(value), {}};
  }

  JsonParseResult parse_literal(std::string_view literal, JsonValue value) {
    if (source_.substr(position_, literal.size()) != literal)
      return failure("Invalid JSON literal");
    position_ += literal.size();
    return {std::move(value), {}};
  }

  bool parse_string(std::string &output) {
    if (!consume('"')) {
      error_ = "JSON object key must be a string";
      return false;
    }
    while (position_ < source_.size()) {
      const auto value = static_cast<unsigned char>(source_[position_++]);
      if (value == '"')
        return true;
      if (value < 0x20) {
        error_ = "JSON string contains a control character";
        return false;
      }
      if (value != '\\') {
        output.push_back(static_cast<char>(value));
        continue;
      }
      if (position_ >= source_.size()) {
        error_ = "JSON escape sequence ended unexpectedly";
        return false;
      }
      switch (source_[position_++]) {
      case '"':
        output.push_back('"');
        break;
      case '\\':
        output.push_back('\\');
        break;
      case '/':
        output.push_back('/');
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u': {
        std::uint32_t codepoint = 0;
        if (!parse_hex4(codepoint))
          return false;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
          if (source_.substr(position_, 2) != "\\u") {
            error_ = "JSON Unicode high surrogate is missing a low surrogate";
            return false;
          }
          position_ += 2;
          std::uint32_t low = 0;
          if (!parse_hex4(low))
            return false;
          if (low < 0xdc00 || low > 0xdfff) {
            error_ = "Invalid JSON Unicode low surrogate";
            return false;
          }
          codepoint = 0x10000 + ((codepoint - 0xd800) << 10U) + (low - 0xdc00);
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
          error_ = "Invalid JSON Unicode low surrogate";
          return false;
        }
        append_utf8(output, codepoint);
        break;
      }
      default:
        error_ = "Invalid JSON string escape";
        return false;
      }
    }
    error_ = "JSON string ended unexpectedly";
    return false;
  }

  bool parse_hex4(std::uint32_t &value) {
    if (source_.size() - position_ < 4) {
      error_ = "JSON Unicode escape ended unexpectedly";
      return false;
    }
    value = 0;
    for (int index = 0; index < 4; ++index) {
      const auto character = source_[position_++];
      value <<= 4U;
      if (character >= '0' && character <= '9')
        value |= static_cast<std::uint32_t>(character - '0');
      else if (character >= 'a' && character <= 'f')
        value |= static_cast<std::uint32_t>(character - 'a' + 10);
      else if (character >= 'A' && character <= 'F')
        value |= static_cast<std::uint32_t>(character - 'A' + 10);
      else {
        error_ = "JSON Unicode escape contains a non-hexadecimal character";
        return false;
      }
    }
    return true;
  }

  static void append_utf8(std::string &output, std::uint32_t value) {
    if (value <= 0x7f) {
      output.push_back(static_cast<char>(value));
    } else if (value <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0U | (value >> 6U)));
      output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0xffff) {
      output.push_back(static_cast<char>(0xe0U | (value >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else {
      output.push_back(static_cast<char>(0xf0U | (value >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    }
  }

  bool consume(char expected) {
    if (position_ >= source_.size() || source_[position_] != expected)
      return false;
    ++position_;
    return true;
  }

  void skip_space() {
    while (position_ < source_.size() &&
           (source_[position_] == ' ' || source_[position_] == '\n' ||
            source_[position_] == '\r' || source_[position_] == '\t'))
      ++position_;
  }

  JsonParseResult failure(std::string message) const {
    return {{},
            std::move(message) + " (offset " + std::to_string(position_) + ")"};
  }

  std::string_view source_;
  std::size_t position_ = 0;
  std::string error_;
};

} // namespace

JsonParseResult parse_json(std::string_view source) {
  return Parser(source).parse();
}
