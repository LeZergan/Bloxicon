#pragma once
#include "AvatarCatalog.h"
#include <memory>

namespace VitaAvatar {
enum class CatalogRead { Present, Missing, Failed };

// Read only the index, once at screen entry. Publish a complete immutable snapshot
// so an older worker can safely finish while the next screen sees updated content.
template<class Reader>
bool loadCatalogSnapshot(const std::string& path, Reader read,
    std::shared_ptr<const Catalog>& snapshot, std::string& error)
{
    std::string encoded;
    const CatalogRead state = read(path, kMaxCatalogBytes, encoded);
    if (state == CatalogRead::Failed)
    { error = "Could not read the offline wardrobe index"; return false; }
    if (state == CatalogRead::Missing && !encoded.empty())
    { error = "Inconsistent wardrobe read"; return false; }
    auto candidate = std::make_shared<Catalog>();
    if (state == CatalogRead::Present && !parseCatalog(encoded, *candidate, error))
        return false; // Includes an existing zero-byte index: it is not a missing file.
    snapshot = std::move(candidate);
    error.clear();
    return true;
}
} // namespace VitaAvatar
