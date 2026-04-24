#include "src/utilities/logger/logger_registry.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace shooting_star {
namespace utilities {
namespace {

using ::std::lock_guard;
using ::std::make_shared;
using ::std::mutex;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::unordered_map;

mutex& RegistryMutex() {
  static mutex mutex;
  return mutex;
}

unordered_map<string, shared_ptr<Logger>>& LoggerMap() {
  static unordered_map<string, shared_ptr<Logger>> logger_map;
  return logger_map;
}

const Logger& GetBlankLoggerLocked() {
  auto& logger_map = LoggerMap();
  const string blank_logger_name(LoggerRegistry::kBlankLoggerName);
  auto it = logger_map.find(blank_logger_name);
  if (it == logger_map.end()) {
    auto blank_logger = make_shared<Logger>(blank_logger_name);
    it = logger_map.emplace(blank_logger_name, ::std::move(blank_logger)).first;
  }

  const Logger& logger = *it->second;
  logger.Info("blank_logger_used",
              {
                  {"blank_logger_name", blank_logger_name},
                  {"reason", "logger is not registered yet"},
              });
  return logger;
}

}  // namespace

string LoggerRegistry::default_logger_name;

void LoggerRegistry::Register(shared_ptr<Logger> logger) {
  if (logger == nullptr) {
    const Logger& blank_logger = Get();
    blank_logger.Info("logger_register_failed",
                      {
                          {"reason", "logger is null"},
                      });
    return;
  }

  const lock_guard<mutex> lock(RegistryMutex());
  LoggerMap()[logger->logger_name()] = ::std::move(logger);
}

void LoggerRegistry::SetDefaultLoggerName(string_view logger_name) {
  const lock_guard<mutex> lock(RegistryMutex());
  if (!default_logger_name.empty()) {
    const auto default_logger_it = LoggerMap().find(default_logger_name);
    const Logger& logger = default_logger_it == LoggerMap().end()
                               ? GetBlankLoggerLocked()
                               : *default_logger_it->second;
    logger.Info("default_logger_name_set_ignored",
                {
                    {"default_logger_name", default_logger_name},
                    {"logger_name_to_set", logger_name},
                    {"reason", "default logger name is already set"},
                });
    return;
  }

  const string logger_name_to_set(logger_name);
  if (LoggerMap().find(logger_name_to_set) == LoggerMap().end()) {
    const Logger& logger = GetBlankLoggerLocked();
    logger.Error("default_logger_name_set_failed",
                 {
                     {"requested_logger_name", logger_name_to_set},
                     {"reason", "logger is not registered"},
                 });
    return;
  }
  default_logger_name = logger_name_to_set;
}

const Logger& LoggerRegistry::Get(string_view logger_name) {
  const lock_guard<mutex> lock(RegistryMutex());
  const string resolved_logger_name =
      logger_name.empty() ? default_logger_name : string(logger_name);
  const string blank_logger_name(kBlankLoggerName);

  if (resolved_logger_name.empty() || resolved_logger_name == blank_logger_name) {
    return GetBlankLoggerLocked();
  }

  const auto it = LoggerMap().find(resolved_logger_name);
  if (it == LoggerMap().end()) {
    return GetBlankLoggerLocked();
  }

  return *it->second;
}

void LoggerRegistry::ClearForTest() {
  const lock_guard<mutex> lock(RegistryMutex());
  LoggerMap().clear();
  default_logger_name.clear();
}

}  // namespace utilities
}  // namespace shooting_star
