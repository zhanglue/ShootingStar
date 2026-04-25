#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace shooting_star {
namespace utilities {

class ConfigHelper {
 public:
  virtual ~ConfigHelper() = default;

  virtual bool Has(::std::string_view key) const = 0;
  virtual ::std::string GetString(::std::string_view key,
                                  ::std::string default_value = "") const = 0;
  virtual int GetInt(::std::string_view key, int default_value) const = 0;
  int GetPositiveInt(::std::string_view key, int default_value) const;
  virtual uint16_t GetUInt16(::std::string_view key,
                             uint16_t default_value) const = 0;
  virtual bool GetBool(::std::string_view key, bool default_value) const = 0;
};

class YamlConfigHelper final : public ConfigHelper {
 public:
  void LoadFromYamlFile(const ::std::string& file_path);
  void LoadFromYamlFileIfExists(const ::std::string& file_path);

  bool Has(::std::string_view key) const override;
  ::std::string GetString(::std::string_view key,
                          ::std::string default_value = "") const override;
  int GetInt(::std::string_view key, int default_value) const override;
  uint16_t GetUInt16(::std::string_view key,
                     uint16_t default_value) const override;
  bool GetBool(::std::string_view key, bool default_value) const override;

  void Set(::std::string key, ::std::string value);
  const ::std::map<::std::string, ::std::string>& values() const;

 private:
  ::std::map<::std::string, ::std::string> values_;
};

}  // namespace utilities
}  // namespace shooting_star
