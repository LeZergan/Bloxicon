#pragma once
#include <boost/filesystem.hpp>

namespace RBX { namespace ContentPath {
// Compare canonical components. A string prefix accepts siblings such as
// content-private when the permitted directory is content.
inline bool contained(const boost::filesystem::path& path, const boost::filesystem::path& root)
{
    boost::system::error_code pathError, rootError;
    const auto resolved = boost::filesystem::canonical(path, pathError);
    auto base = boost::filesystem::canonical(root, rootError);
    if (pathError || rootError) return false;
    // Legacy Boost preserves a trailing directory separator as a final dot
    // component. The engine deliberately appends it to every content root.
    // Strip only that directory spelling; preserve drive/device root parts.
    while (base.has_relative_path() && (base.filename().empty() || base.filename() == "."))
        base = base.parent_path();
    auto part = resolved.begin();
    for (auto expected = base.begin(); expected != base.end(); ++expected, ++part)
        if (part == resolved.end() || *part != *expected) return false;
    return true;
}
} }
