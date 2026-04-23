#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace shooting_star {
namespace clients {

inline void PrintRunStartedAtUtc() {
  const ::std::chrono::system_clock::time_point now =
      ::std::chrono::system_clock::now();
  const ::std::time_t now_time = ::std::chrono::system_clock::to_time_t(now);
  ::std::cout << "Run started at UTC: "
              << ::std::put_time(::std::gmtime(&now_time), "%Y-%m-%dT%H:%M:%SZ")
              << "\n";
}

}  // namespace clients
}  // namespace shooting_star
