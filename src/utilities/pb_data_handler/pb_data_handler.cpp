#include "pb_data_handler.h"
#include <google/protobuf/util/json_util.h>
#include <fstream>

using string = ::std::string;
using Message = ::google::protobuf::Message;

namespace shooting_star {
namespace utilities {

bool PBDataHandler::PBToJson(const Message& message, string* json_out, string* error_msg) {
    if (!json_out) {
        if (error_msg) {
          *error_msg = "PBDataHandler::PBToJson failed: json_out is nullptr";
        }
        return false;
    }

    ::google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;

    auto status = ::google::protobuf::util::MessageToJsonString(message, json_out, options);
    if (!status.ok()) {
        if (error_msg) {
          *error_msg = "PBDataHandler::PBToJson failed: " + status.ToString();
        }
        return false;
    }

    return true;
}

bool PBDataHandler::JsonToPB(const string& json_in, Message* message, string* error_msg) {
    if (!message) {
        if (error_msg) {
          *error_msg = "PBDataHandler::JsonToPB failed: message is nullptr";
        }
        return false;
    }

    ::google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;

    auto status = ::google::protobuf::util::JsonStringToMessage(json_in, message, options);
    if (!status.ok()) {
        if (error_msg) {
          *error_msg = "PBDataHandler::JsonToPB failed: " + status.ToString();
        }
        return false;
    }

    return true;
}

bool PBDataHandler::JsonFileToPB(const string& file_path, Message* message, string* error_msg) {
    if (!message) {
        if (error_msg) {
          *error_msg = "PBDataHandler::JsonFileToPB failed: message is nullptr";
        }
        return false;
    }

    ::std::ifstream fin(file_path);
    if (!fin.is_open()) {
        if (error_msg) {
          *error_msg = "PBDataHandler::JsonFileToPB failed: cannot open file " + file_path;
        }
        return false;
    }

    string json_content(
        (::std::istreambuf_iterator<char>(fin)),
        ::std::istreambuf_iterator<char>());
    fin.close();

    return JsonToPB(json_content, message, error_msg);
}

bool PBDataHandler::PBToJsonFile(const Message& message, const string& file_path, string* error_msg) {
    string json_str;
    if (!PBToJson(message, &json_str, error_msg)) {
        return false;
    }

    ::std::ofstream fout(file_path);
    if (!fout.is_open()) {
        if (error_msg) *error_msg = "PBDataHandler::PBToJsonFile failed: cannot open file " + file_path;
        return false;
    }

    fout << json_str;
    fout.close();
    return true;
}

}  // namespace utilities
}  // namespace shooting_star
