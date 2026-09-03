#ifndef LILYGO_UI_STORE_JSON_HPP
#define LILYGO_UI_STORE_JSON_HPP

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class JsonValue {
public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;

  JsonValue() = default;
  explicit JsonValue(bool value) : value_(value) {}
  explicit JsonValue(std::int64_t value) : value_(value) {}
  explicit JsonValue(std::string value) : value_(std::move(value)) {}
  explicit JsonValue(Array value) : value_(std::move(value)) {}
  explicit JsonValue(Object value) : value_(std::move(value)) {}

  [[nodiscard]] bool is_null() const noexcept;
  [[nodiscard]] const bool *boolean() const noexcept;
  [[nodiscard]] const std::int64_t *integer() const noexcept;
  [[nodiscard]] const std::string *string() const noexcept;
  [[nodiscard]] const Array *array() const noexcept;
  [[nodiscard]] const Object *object() const noexcept;

private:
  std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object>
      value_{};
};

struct JsonParseResult {
  JsonValue value;
  std::string error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

[[nodiscard]] JsonParseResult parse_json(std::string_view source);

#endif
