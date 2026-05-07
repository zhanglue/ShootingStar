#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/struct.pb.h>

namespace shooting_star {
namespace utilities {

class JsonParser final {
 public:
  JsonParser() = delete;

  static ::google::protobuf::Struct ParseObject(::std::string_view json,
                                                ::std::string_view context);
  static ::google::protobuf::Value ParseValue(::std::string_view json,
                                              ::std::string_view context);

  static const ::google::protobuf::Value* FindField(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name);
  static const ::google::protobuf::Value& RequiredField(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static bool IsNull(const ::google::protobuf::Value& value);

  static uint64_t ReadUint64Value(const ::google::protobuf::Value& value,
                                  ::std::string_view context);
  static int64_t ReadInt64Value(const ::google::protobuf::Value& value,
                                ::std::string_view context);
  static double ReadDoubleValue(const ::google::protobuf::Value& value,
                                ::std::string_view context);
  static ::std::string ReadStringValue(
      const ::google::protobuf::Value& value,
      ::std::string_view context);

  static uint64_t ReadRequiredUint64(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static ::std::string ReadOptionalString(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static int ReadOptionalInt(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static int64_t ReadOptionalInt64(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static double ReadOptionalDouble(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static const ::google::protobuf::Struct* ReadOptionalStruct(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
  static ::std::vector<::std::string> ReadOptionalStringList(
      const ::google::protobuf::Map<::std::string,
                                    ::google::protobuf::Value>& fields,
      ::std::string_view field_name,
      ::std::string_view context);
};

}  // namespace utilities
}  // namespace shooting_star
