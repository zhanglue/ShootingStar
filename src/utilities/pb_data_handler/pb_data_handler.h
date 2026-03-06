#pragma once

#include <string>
#include <google/protobuf/message.h>

namespace shooting_star {
namespace utilities {

class PBDataHandler {
public:
    static bool JsonToPB(
        const ::std::string& json_str,
        ::google::protobuf::Message* message,
        ::std::string* error_msg = nullptr);

    static bool PBToJson(
        const ::google::protobuf::Message& message,
        ::std::string* json_out,
        ::std::string* error_msg = nullptr);

    static bool JsonFileToPB(
        const ::std::string& file_path,
        ::google::protobuf::Message* message,
        ::std::string* error_msg = nullptr);

    static bool PBToJsonFile(
        const ::google::protobuf::Message& message,
        const ::std::string& file_path,
        ::std::string* error_msg = nullptr);
};

}  // namespace utilities
}  // namespace shooting_star
