#pragma once

#include <string>
#include <string_view>

namespace shooting_star {
namespace utilities {

::std::string Base64Encode(::std::string_view input);

::std::string ResolveWorkspaceRelativePath(
    const ::std::string& path,
    const ::std::string& executable_path = "");

void TrimLeadingSlashes(::std::string& value);

void TrimTrailingSlashes(::std::string& value);

void TrimWhitespace(::std::string_view& value);

}  // namespace utilities
}  // namespace shooting_star
