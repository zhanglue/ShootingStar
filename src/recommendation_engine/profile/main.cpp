#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "src/recommendation_engine/profile/local_file_profile_store.h"
#include "src/recommendation_engine/profile/profile_service.h"
#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

ABSL_FLAG(uint16_t, port, 50100, "Server port for the service");
ABSL_FLAG(
    ::std::string,
    profile_store_type,
    "local",
    "Profile store type. Supported values: local.");
ABSL_FLAG(
    ::std::string,
    profile_data_path,
    "tests/testdata/recommendation_engine/profile/demo_profiles.json",
    "Path to the profile data JSON file.");

namespace {

using ::std::cout;
using ::std::endl;
using ::std::invalid_argument;
using ::std::make_unique;
using ::std::string;
using ::std::unique_ptr;

unique_ptr<::recommendation_engine::ProfileStore> CreateProfileStore(
    const string& profile_store_type,
    const string& profile_data_path,
    const string& executable_path) {
  if (profile_store_type == "local") {
    const string resolved_profile_data_path =
        ::shooting_star::utilities::ResolveWorkspaceRelativePath(
            profile_data_path, executable_path);
    cout << "Profile data path: " << resolved_profile_data_path << endl;
    return make_unique<::recommendation_engine::LocalFileProfileStore>(
        resolved_profile_data_path);
  }

  throw invalid_argument(
      ::absl::StrFormat("Unsupported profile store type: %s", profile_store_type));
}

}  // namespace

int main(int argc, char** argv) {
  ::absl::ParseCommandLine(argc, argv);
  const ::std::string server_address =
      ::absl::StrFormat("0.0.0.0:%d", ::absl::GetFlag(FLAGS_port));
  const ::std::string profile_data_path = ::absl::GetFlag(FLAGS_profile_data_path);
  const ::std::string profile_store_type = ::absl::GetFlag(FLAGS_profile_store_type);

  try {
    ::std::cout << "Profile store type: " << profile_store_type << ::std::endl;
    ::std::unique_ptr<::recommendation_engine::ProfileStore> profile_store =
        CreateProfileStore(profile_store_type, profile_data_path, argv[0]);
    ::recommendation_engine::ProfileServiceImpl service(profile_store.get());

    ::grpc::EnableDefaultHealthCheckService(true);
    ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    ::std::cout << "Server listening on " << server_address << ::std::endl;
    server->Wait();
  } catch (const ::std::exception& ex) {
    ::std::cerr << ex.what() << ::std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
