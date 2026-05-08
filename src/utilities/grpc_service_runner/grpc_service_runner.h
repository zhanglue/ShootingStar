#pragma once

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"

namespace shooting_star {
namespace utilities {

template <typename ServiceT>
int RunGrpcService(const char* service_name, int argc, char** argv) {
  static_assert(::std::is_base_of_v<::grpc::Service, ServiceT>,
                "ServiceT must be a grpc::Service implementation.");

  try {
    const GlobalConfig& config = GlobalConfig::Initialize(service_name);
    ConfigYAML::ApplyStartupFile(argc, argv, argv[0]);
    ConfigArguments::Apply(argc, argv);

    auto service_logger = ::std::make_shared<Logger>(service_name);
    service_logger->SetMinLogLevel(config.GetLogLevel());
    LoggerRegistry::Register(::std::move(service_logger));
    LoggerRegistry::SetDefaultLoggerName(service_name);

    const Logger& logger = LoggerRegistry::Get();
    logger.Info("config_loaded", {{"config_path", config.GetConfigPath()}});
    config.LogResolvedConfig(logger);

    const ::std::string server_address = config.GetListenAddress();
    ::std::unique_ptr<ServiceT> server = ServiceT::Create(config);
    if (server == nullptr) {
      throw ::std::runtime_error("Service factory returned null.");
    }

    ::grpc::EnableDefaultHealthCheckService(true);
    ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ::grpc::ServerBuilder builder;
    builder.experimental().SetInterceptorCreators(
        CreateServerLoggingInterceptorCreators(logger));
    builder.AddListeningPort(server_address,
                             ::grpc::InsecureServerCredentials());
    builder.RegisterService(server.get());

    ::std::unique_ptr<::grpc::Server> grpc_server(builder.BuildAndStart());
    if (grpc_server == nullptr) {
      throw ::std::runtime_error("Failed to build and start gRPC server.");
    }

    logger.Info("server_started", {{"listen_address", server_address}});
    grpc_server->Wait();
  } catch (const ::std::exception& ex) {
    const Logger& logger = LoggerRegistry::Get();
    logger.Error("server_startup_failed", {{"error", ex.what()}});
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

}  // namespace utilities
}  // namespace shooting_star
