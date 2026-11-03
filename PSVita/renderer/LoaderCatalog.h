#include "LoaderArtworkPolicy.h"
#include "LoaderGameInfo.h"
#include "LoaderArchiveIdentity.h"
#include "LoaderMetadataPolicy.h"
#include "CatalogDownloadPolicy.h"
#include "OwnedPlaceCatalog.h"
#include "HostedPlaceMetadata.h"
#pragma once
#if defined(RBX_PLATFORM_PSVITA)
#include "util/VitaHttpRequestPolicy.h"
#include "util/BloxiconPolicy.h"
#endif

#include "util/Http.h"
#include "VitaPlaceCache.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>

namespace VitaBrowser {

struct CatalogItem
{
    std::string contentHash;
    std::string name;
    std::string url;
    unsigned long long size;
    bool featured;
    std::string source;
    bool githubRawApi;
    unsigned long long placeId;
    std::string year;
    std::string creator;
    std::string description;
    std::string thumbnailUrl;
    bool vitaTested=false;
    bool testerRecommended=false;
    std::string vitaStatus,uploadedBy,catalogueGameId;
    std::string serverStatus,testedBy,artworkKind,launchScope;
    unsigned long long launches=0,launchSince=0;bool launchesKnown=false;
    bool serverKnown=false,serverAvailable=false;unsigned long long serverSampledAt=0;

    CatalogItem(const std::string& name = std::string(),
                const std::string& url = std::string(),
                unsigned long long size = 0,
                bool featured = false,
                const std::string& source = std::string(),
                bool githubRawApi = false,
                unsigned long long placeId = 0,
                const std::string& year = std::string(),
                const std::string& creator = std::string(),
                const std::string& description = std::string(),
                const std::string& thumbnailUrl = std::string())
        : name(name)
        , url(url)
        , size(size)
        , featured(featured)
        , source(source)
        , githubRawApi(githubRawApi)
        , placeId(placeId)
        , year(year)
        , creator(creator)
        , description(description)
        , thumbnailUrl(thumbnailUrl)
    {}
};

struct InstalledPlaceMetadata
{
    CatalogItem item;
    unsigned long long installedSize;
    bool valid;
    bool identityTrusted;

    InstalledPlaceMetadata(): installedSize(0), valid(false), identityTrusted(false) {}
};

struct PlaceRuntimeIdentity
{
    long long placeId;
    long long universeId;
    long long creatorId;
    int creatorType;
    unsigned long long fileSize;
    bool valid;

    PlaceRuntimeIdentity()
        : placeId(0), universeId(0), creatorId(0), creatorType(0), fileSize(0), valid(false) {}
};

struct PublicPlaceMetadata
{
    long long placeId;
    long long universeId;
    std::string name;
    std::string description;
    std::string creator;
    std::string year;
    std::string thumbnailPath;
    VitaBrowser::GameInfo info;
    bool valid;

    PublicPlaceMetadata(): placeId(0), universeId(0), valid(false) {}
};

inline std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline bool supportedPlaceName(const std::string& name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    const std::string ext = lowerCopy(name.substr(dot));
    return ext == ".rbxl" || ext == ".rbxlx" || ext == ".rbxm" || ext == ".rbxmx";
}

inline std::string safeFileName(std::string name)
{
    const size_t query = name.find_first_of("?#");
    if (query != std::string::npos)
        name.resize(query);
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    std::string decoded;
    decoded.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == '%' && i + 2 < name.size())
        {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(name[i + 1]);
            const int lo = hex(name[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(name[i]);
    }

    for (char& c : decoded)
        if (static_cast<unsigned char>(c) < 32 || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == '/' || c == '\\')
            c = '_';

    while (!decoded.empty() && (decoded.back() == ' ' || decoded.back() == '.'))
        decoded.pop_back();
    if (decoded.size() > 96)
    {
        const size_t dot = decoded.find_last_of('.');
        const std::string ext = dot == std::string::npos ? std::string() : decoded.substr(dot);
        decoded.resize(std::min<size_t>(80, decoded.size()));
        if (!ext.empty() && decoded.size() + ext.size() <= 96 &&
            lowerCopy(decoded).rfind(lowerCopy(ext)) != decoded.size() - ext.size())
            decoded += ext;
    }
    return decoded;
}

inline unsigned long long fileSize(const std::string& path);

inline bool fetchExternal(const std::string& url, std::string& data, std::string& error, bool githubRawApi = false,int* httpStatus=NULL)
{
    data.clear();
    if(httpStatus)*httpStatus=0;
    try
    {
        RBX::Http request(url);
        // GitHub's API rejects anonymous clients without User-Agent. The
        // standalone Vita host bypasses the desktop code that normally fills
        // Roblox's UA, so identify only this public downloader (never a user).
        request.additionalHeaders["User-Agent"] = "Bloxicon-Vita/0.3";
        request.additionalHeaders["Accept"] = githubRawApi
            ? "application/vnd.github.raw+json"
            : "application/vnd.github+json, application/octet-stream;q=0.9, */*;q=0.8";
        // The loader must never feel hung when an archive is offline. Places
        // can still be installed from the built-in featured URLs or a cached
        // catalog, so keep public-catalog network waits bounded.
        request.setConnectionTimeout(5000);
        request.setResponseTimeout(15000);
        request.get(data, true);
        if (data.empty())
        {
            error = "Server returned an empty response.";
            return false;
        }
        return true;
    }
    catch (const RBX::http_status_error& e)
    {
        if(httpStatus)*httpStatus=e.statusCode;
        error=std::string(e.what()).substr(0,110);return false;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        if (error.size() > 110)
            error.resize(110);
        return false;
    }
}

inline bool fetchExternalToFile(const std::string& url, const std::string& path,
                                size_t maxBytes, std::string& error,
                                bool githubRawApi, bool allowPersistentResume,
                                size_t& totalBytes, unsigned responseTimeoutMs = 120000)
{
    totalBytes = 0;
    size_t resumeOffset = allowPersistentResume ? static_cast<size_t>(fileSize(path)) : 0;
    if (!allowPersistentResume)
        std::remove(path.c_str());
    if (resumeOffset > maxBytes)
    {
        std::remove(path.c_str());
        resumeOffset = 0;
    }

    bool restartedFromZero = false;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        try
        {
            RBX::Http request(url);
            request.additionalHeaders["User-Agent"] = "Bloxicon-Vita/0.3";
            request.additionalHeaders["Accept"] = githubRawApi
                ? "application/vnd.github.raw+json"
                : "application/octet-stream, */*;q=0.8";
            request.setConnectionTimeout(5000);
            // Large place files need longer than catalog JSON, but still must
            // fail in bounded time so the loader always returns to input.
            request.setResponseTimeout(responseTimeoutMs);

            size_t reportedBytes = resumeOffset;
            request.getToFile(path, maxBytes, resumeOffset, reportedBytes, true);
            totalBytes = reportedBytes;

            const size_t actualBytes = static_cast<size_t>(fileSize(path));
            if (actualBytes != totalBytes)
            {
                error = "Downloaded size did not match the staged file.";
                return false;
            }
            if (!totalBytes)
            {
                error = "Server returned an empty response.";
                return false;
            }
            return true;
        }
        catch (const RBX::http_status_error& e)
        {
            // HTTP error bodies may already have reached the sink. They are not
            // resumable place bytes, so discard them before any retry.
            std::remove(path.c_str());
            resumeOffset = 0;
            totalBytes = 0;
            error = e.what();
            const bool transient = e.statusCode == 408 || e.statusCode == 429 ||
                (e.statusCode >= 500 && e.statusCode <= 599);
            if (!transient)
                return false;
        }
        catch (const std::exception& e)
        {
            const size_t actualBytes = static_cast<size_t>(fileSize(path));
            totalBytes = actualBytes;
            error = e.what();
            if (error.size() > 110)
                error.resize(110);
            if (actualBytes > maxBytes)
            {
                std::remove(path.c_str());
                totalBytes = 0;
                return false;
            }

            // If the failed request made progress, keep it and resume. If a
            // server rejects Range/resume without writing anything, retry once
            // from byte zero rather than looping forever on the same offset.
            if (actualBytes > resumeOffset)
            {
                resumeOffset = actualBytes;
                continue;
            }
            if (resumeOffset && !restartedFromZero)
            {
                std::remove(path.c_str());
                resumeOffset = 0;
                totalBytes = 0;
                restartedFromZero = true;
                continue;
            }
        }
    }
    return false;
}

inline bool fileExists(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    std::fclose(file);
    return true;
}

inline unsigned long long fileSize(const std::string& path)
{
    struct stat info = {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)
        ? static_cast<unsigned long long>(info.st_size)
        : 0;
}

inline bool ux0FreeBytes(unsigned long long& freeBytes)
{
    uint64_t capacity = 0;
    uint64_t freeSize = 0;
    if (sceAppMgrGetDevInfo("ux0:", &capacity, &freeSize) < 0)
        return false;
    freeBytes = static_cast<unsigned long long>(freeSize);
    return true;
}

inline bool downloadResumeMetadataMatches(const std::string& tempPath, const CatalogItem& item);

inline bool downloadHasWorkingSpace(const CatalogItem& item, const std::string& tempPath,
                                    std::string& error)
{
    // Keep a little room for filesystem bookkeeping and the tiny install
    // metadata sidecar. Existing installed bytes are renamed during commit, not
    // copied, so only the unfinished staged bytes need extra capacity.
    const unsigned long long reserveBytes = 512ull * 1024ull;
    unsigned long long freeBytes = 0;
    if (!ux0FreeBytes(freeBytes))
        return true; // the write path still reports a real storage error

    unsigned long long stagedBytes = 0;
    if (downloadResumeMetadataMatches(tempPath, item))
    {
        stagedBytes = fileSize(tempPath);
        if (item.size && stagedBytes > item.size)
            stagedBytes = 0;
    }

    const unsigned long long remainingBytes = item.size && item.size > stagedBytes
        ? item.size - stagedBytes
        : 0;
    const unsigned long long requiredBytes = remainingBytes + reserveBytes;
    if ((item.size && freeBytes < requiredBytes) || (!item.size && freeBytes < reserveBytes))
    {
        std::ostringstream message;
        message << "Not enough free storage ("
            << (freeBytes / 1024ull) << " KiB free";
        if (item.size)
            message << ", " << (remainingBytes / 1024ull) << " KiB download remaining";
        message << ").";
        error = message.str();
        return false;
    }
    return true;
}

inline std::string metadataValue(std::string value)
{
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return value;
}

inline void recoverAtomicTarget(const std::string& finalPath)
{
    const std::string backupPath = finalPath + ".previous";
    if (!fileExists(finalPath) && fileExists(backupPath))
        std::rename(backupPath.c_str(), finalPath.c_str());
    else if (fileExists(finalPath) && fileExists(backupPath))
        std::remove(backupPath.c_str());
}

inline bool commitAtomicTemp(const std::string& finalPath, const std::string& tempPath)
{
    const std::string backupPath = finalPath + ".previous";
    recoverAtomicTarget(finalPath);

    // Never delete the last known-good file just to make rename succeed: an
    // out-of-space or I/O error must leave the old install usable.
    const bool hadExisting = fileExists(finalPath);
    if (hadExisting)
    {
        std::remove(backupPath.c_str());
        if (std::rename(finalPath.c_str(), backupPath.c_str()) != 0)
            return false;
    }

    if (std::rename(tempPath.c_str(), finalPath.c_str()) != 0)
    {
        if (hadExisting)
            std::rename(backupPath.c_str(), finalPath.c_str());
        return false;
    }
    if (hadExisting)
        std::remove(backupPath.c_str());
    return true;
}

inline bool writeAtomic(const std::string& finalPath, const std::string& data)
{
    const std::string tempPath = finalPath + ".download";
    recoverAtomicTarget(finalPath);
    std::remove(tempPath.c_str());

    {
        std::ofstream output(tempPath.c_str(), std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output)
        {
            std::remove(tempPath.c_str());
            return false;
        }
    }

    if (!commitAtomicTemp(finalPath, tempPath))
    {
        std::remove(tempPath.c_str());
        return false;
    }
    return true;
}

inline bool commitStagedFile(const std::string& finalPath, const std::string& tempPath)
{
    const std::string backupPath = finalPath + ".previous";

    if (!fileExists(finalPath) && fileExists(backupPath))
        std::rename(backupPath.c_str(), finalPath.c_str());
    else if (fileExists(finalPath) && fileExists(backupPath))
        std::remove(backupPath.c_str());

    const bool hadExisting = fileExists(finalPath);
    if (hadExisting)
    {
        std::remove(backupPath.c_str());
        if (std::rename(finalPath.c_str(), backupPath.c_str()) != 0)
            return false;
    }

    if (std::rename(tempPath.c_str(), finalPath.c_str()) != 0)
    {
        if (hadExisting)
            std::rename(backupPath.c_str(), finalPath.c_str());
        return false;
    }

    if (hadExisting)
        std::remove(backupPath.c_str());
    return true;
}

inline bool looksLikeRobloxPlace(const std::string& data, std::string& error)
{
    if (data.size() < 16)
    {
        error = "Downloaded file is too small.";
        return false;
    }

    static const char binaryMagic[] = "<roblox!";
    if (data.size() >= 8 && std::memcmp(data.data(), binaryMagic, 8) == 0)
        return true;

    static const char lfsPrefix[] = "version https://git-lfs.github.com/spec/v1";
    if (data.compare(0, sizeof(lfsPrefix) - 1, lfsPrefix) == 0)
    {
        error = "Source returned a Git LFS pointer instead of place data.";
        return false;
    }

    // RBXLX/RBXMX commonly start with an XML declaration. Limit the scan so an
    // HTML error page that happens to mention Roblox later cannot be installed.
    const size_t probe = std::min<size_t>(data.size(), 1024);
    const std::string prefix = data.substr(0, probe);
    if (prefix.find("<roblox") != std::string::npos)
        return true;

    error = "Response is not Bloxicon binary or XML place/model data.";
    return false;
}

inline bool looksLikeRobloxPlaceFile(const std::string& path, std::string& error)
{
    const unsigned long long bytes = fileSize(path);
    if (bytes < 16)
    {
        error = "Downloaded file is too small.";
        return false;
    }

    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
    {
        error = "Downloaded file could not be reopened.";
        return false;
    }
    char prefix[1024];
    input.read(prefix, sizeof(prefix));
    const std::streamsize count = input.gcount();
    if (count <= 0)
    {
        error = "Downloaded file could not be read.";
        return false;
    }
    return looksLikeRobloxPlace(std::string(prefix, static_cast<size_t>(count)), error);
}

inline std::string downloadResumeMetadataPath(const std::string& tempPath)
{
    return tempPath + ".meta";
}

inline bool writeDownloadResumeMetadata(const std::string& tempPath, const CatalogItem& item)
{
    std::ostringstream text;
    text << "version=1\n"
        << "url=" << metadataValue(item.url) << '\n'
        << "expected_size=" << item.size << '\n'
        << "github_raw_api=" << (item.githubRawApi ? 1 : 0) << '\n';
    return writeAtomic(downloadResumeMetadataPath(tempPath), text.str());
}

inline bool downloadResumeMetadataMatches(const std::string& tempPath, const CatalogItem& item)
{
    std::ifstream input(downloadResumeMetadataPath(tempPath).c_str());
    if (!input)
        return false;

    unsigned version = 0;
    unsigned long long expectedSize = 0;
    bool githubRawApi = false;
    std::string url;
    std::string line;
    while (std::getline(input, line))
    {
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "version") version = static_cast<unsigned>(std::strtoul(value.c_str(), NULL, 10));
        else if (key == "url") url = value;
        else if (key == "expected_size") expectedSize = std::strtoull(value.c_str(), NULL, 10);
        else if (key == "github_raw_api") githubRawApi = value == "1";
    }
    return version == 1 && url == item.url && expectedSize == item.size &&
        githubRawApi == item.githubRawApi;
}

inline void clearDownloadResumeState(const std::string& tempPath)
{
    std::remove(tempPath.c_str());
    const std::string metadata = downloadResumeMetadataPath(tempPath);
    std::remove(metadata.c_str());
    std::remove((metadata + ".download").c_str());
    std::remove((metadata + ".previous").c_str());
}

inline bool fetchExternalToFile(const CatalogItem& item, const std::string& tempPath,
                                size_t maxBytes, size_t& totalBytes, std::string& error)
{
    if(!CatalogDownloadPolicy::ownedObject(item.url)||!ArchiveIdentity::validHash(item.contentHash)){
        error="Refresh the Bloxicon archive to download this original.";return false;
    }
    std::string requestUrl=item.url;
    bool rawApi=item.githubRawApi;
    const std::string alternate=CatalogDownloadPolicy::blobFallback(item.url,item.contentHash);
    if (!downloadResumeMetadataMatches(tempPath, item))
        clearDownloadResumeState(tempPath);

    if (!fileExists(tempPath))
    {
        if (!writeDownloadResumeMetadata(tempPath, item))
        {
            error = "Could not save download resume metadata.";
            return false;
        }
    }

    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        size_t resumeOffset = static_cast<size_t>(fileSize(tempPath));
        if (resumeOffset > maxBytes || (item.size && resumeOffset > item.size))
        {
            clearDownloadResumeState(tempPath);
            if (!writeDownloadResumeMetadata(tempPath, item))
            {
                error = "Could not reset download resume metadata.";
                return false;
            }
            resumeOffset = 0;
        }
        if (item.size && resumeOffset == item.size)
        {
            totalBytes = resumeOffset;
            return true; // completed before a previous launch was interrupted
        }

        try
        {
            RBX::Http request(requestUrl);
            request.shouldRetry = false; // resume/retry is managed here
            request.additionalHeaders["User-Agent"] = "Bloxicon-Vita/0.3";
            request.additionalHeaders["Accept"] = rawApi
                ? "application/vnd.github.raw+json"
                : "application/octet-stream, application/xml;q=0.9, text/xml;q=0.8, */*;q=0.5";
            request.setConnectionTimeout(12000);
            request.setResponseTimeout(120000);
            request.getToFile(tempPath, maxBytes, resumeOffset, totalBytes, true);

            // A cached tree can outlive its branch revision. If raw/main now
            // serves different bytes, fetch the exact advertised Git blob.
            // The final install checksum remains a separate mandatory gate.
            if(attempt<2 && !alternate.empty() && requestUrl!=alternate) {
                std::atomic<bool> cancelled(false);
                const bool sizeMatches=!item.size||totalBytes==item.size;
                if(!sizeMatches || ArchiveIdentity::fileHash(tempPath,totalBytes,cancelled)!=item.contentHash) {
                    clearDownloadResumeState(tempPath);requestUrl=alternate;rawApi=true;
                    if(!writeDownloadResumeMetadata(tempPath,item)){error="Could not reset changed download.";return false;}
                    continue;
                }
            }
            if (item.size && totalBytes != item.size)
                throw std::runtime_error("Downloaded size does not match provider metadata");
            return true;
        }
        catch (const RBX::http_status_error& status)
        {
            // Error responses may have bodies; never append one to a future
            // resume. Retry only temporary server failures.
            clearDownloadResumeState(tempPath);
            if(attempt<2 && !alternate.empty() && requestUrl!=alternate &&
                (status.statusCode==403||status.statusCode==404||CatalogDownloadPolicy::transient(status.statusCode))) {
                requestUrl=alternate;rawApi=true;
                if(!writeDownloadResumeMetadata(tempPath,item)){error="Could not reset failed download.";return false;}
                continue;
            }
            if (!CatalogDownloadPolicy::transient(status.statusCode) || attempt == 2)
            {
                error = status.what();
                return false;
            }
            if (!writeDownloadResumeMetadata(tempPath, item))
            {
                error = "Could not reset failed download.";
                return false;
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
            const bool storageFailure = error.find("storage") != std::string::npos;
            const bool invalidPartial =
                error.find("safety limit") != std::string::npos ||
                error.find("size does not match") != std::string::npos;
            if (storageFailure)
            {
                // Keep a valid partial + matching sidecar. Once the player
                // frees space, the next Install/Update resumes from this byte.
                return false;
            }
            if (invalidPartial)
            {
                clearDownloadResumeState(tempPath);
                return false;
            }

            const size_t after = static_cast<size_t>(fileSize(tempPath));
            // If a resumed request made no progress, the host may not support
            // byte ranges. Restart once from zero instead of retrying the same
            // unusable Range forever.
            if (resumeOffset && after == resumeOffset)
            {
                clearDownloadResumeState(tempPath);
                if (!writeDownloadResumeMetadata(tempPath, item))
                    return false;
            }
            if (attempt == 2)
                return false; // keep a valid partial + metadata for next launch
        }
    }
    return false;
}

inline std::string installMetadataPath(const std::string& placePath)
{
    return placePath + ".vita-meta";
}

inline unsigned long long metadataSourceStamp(const std::string& path){
    struct stat info={};return ::stat(path.c_str(),&info)==0&&S_ISREG(info.st_mode)&&info.st_mtime>=0?
        static_cast<unsigned long long>(info.st_mtime):0;
}
inline bool metadataBinding(const MetadataPolicy::Document& document,const std::string& placePath){
    unsigned long long bytes=0,stamp=0;std::string decoded;
    return document.number("source_size",bytes,~0ull,true)&&document.number("source_stamp",stamp,~0ull,true)&&
        document.has("source_path_hex")&&GameInfo::decode(document.text("source_path_hex"),decoded)&&decoded==placePath&&
        bytes==fileSize(placePath)&&stamp==metadataSourceStamp(placePath);
}
inline void writeMetadataBinding(std::ostringstream& text,const std::string& placePath){
    text<<"source_path_hex="<<GameInfo::encode(placePath)<<'\n'<<"source_size="<<fileSize(placePath)<<'\n'
        <<"source_stamp="<<metadataSourceStamp(placePath)<<'\n';
}

inline bool writeInstallMetadata(const std::string& placePath, const CatalogItem& item,
                                 unsigned long long installedSize)
{
    std::ostringstream text;
    text << "version=3\n"
        << "name=" << metadataValue(item.name) << '\n'
        << "url=" << metadataValue(item.url) << '\n'
        << "source=" << metadataValue(item.source) << '\n'
        << "catalog_size=" << item.size << '\n'
        << "installed_size=" << installedSize << '\n'
        << "github_raw_api=" << (item.githubRawApi ? 1 : 0) << '\n'
        << "place_id=" << item.placeId << '\n'
        << "year=" << metadataValue(item.year) << '\n'
        << "creator=" << metadataValue(item.creator) << '\n'
        << "description=" << metadataValue(item.description) << '\n'
        << "thumbnail_url=" << metadataValue(item.thumbnailUrl) << '\n'
        << "content_hash=" << item.contentHash << '\n'
        << "catalogue_game_id=" << metadataValue(item.catalogueGameId) << '\n'
        << "uploaded_by=" << metadataValue(item.uploadedBy) << '\n'
        << "vita_status=" << metadataValue(item.vitaStatus) << '\n';
    writeMetadataBinding(text,placePath);
    return writeAtomic(installMetadataPath(placePath), text.str());
}

inline bool readInstallMetadata(const std::string& placePath, InstalledPlaceMetadata& result)
{
    MetadataPolicy::Document document;if(!document.read(installMetadataPath(placePath),256u*1024u))return false;
    InstalledPlaceMetadata parsed;unsigned version=0;
    if(!document.number("version",version,3,true)||version<1||!document.number("catalog_size",parsed.item.size)||
        !document.number("installed_size",parsed.installedSize)||!document.number("place_id",parsed.item.placeId,LLONG_MAX))return false;
    parsed.item.name=document.text("name");parsed.item.url=document.text("url");parsed.item.source=document.text("source");
    parsed.item.githubRawApi=document.text("github_raw_api")=="1";
    if(document.has("github_raw_api")&&document.text("github_raw_api")!="0"&&!parsed.item.githubRawApi)return false;
    parsed.item.year=document.text("year");parsed.item.creator=document.text("creator");parsed.item.description=document.text("description");
    parsed.item.thumbnailUrl=document.text("thumbnail_url");parsed.item.contentHash=document.text("content_hash");
    parsed.item.catalogueGameId=document.text("catalogue_game_id");parsed.item.uploadedBy=document.text("uploaded_by");parsed.item.vitaStatus=document.text("vita_status");
    if(!parsed.item.contentHash.empty()&&!ArchiveIdentity::validHash(parsed.item.contentHash))return false;
    if(parsed.item.name.empty()||parsed.item.url.empty())return false;
    if(version==3){if(!metadataBinding(document,placePath)||!parsed.installedSize||parsed.installedSize!=fileSize(placePath))return false;
        parsed.identityTrusted=true;
    }else{
        // Earlier catalogues merged namesakes. Retain source/update bookkeeping,
        // but re-resolve their identity/credits/artwork against original bytes.
        parsed.item.placeId=0;parsed.item.thumbnailUrl.clear();parsed.item.contentHash.clear();
        parsed.item.source.clear();parsed.item.year.clear();parsed.item.creator.clear();parsed.item.description.clear();
    }
    parsed.valid = true;
    result = parsed;
    return true;
}

inline std::string runtimeIdentityPath(const std::string& placePath)
{
    return "ux0:/data/RBXV00001/system/game-info/"+artworkKey(placePath)+".runtime";
}

inline std::string publicMetadataPath(const std::string& placePath,long long placeId=0)
{
    if(placePath.empty()&&placeId>0)return "ux0:/data/RBXV00001/system/game-info/online-"+std::to_string(placeId)+".details";
    return "ux0:/data/RBXV00001/system/game-info/" + artworkKey(placePath) + ".details";
}

inline std::string publicThumbnailPath(const std::string& placePath,long long placeId=0)
{
    if(placeId>0)return "ux0:/data/RBXV00001/system/thumbnails/public-v2-"+artworkKey(placePath)+"-"+std::to_string(placeId)+".png";
    return "ux0:/data/RBXV00001/system/thumbnails/public-"+artworkKey(placePath)+".png";
}

inline std::string metadataSingleLine(std::string value)
{
    for (char& c : value)
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    return value;
}

inline bool readPlaceRuntimeIdentity(const std::string& placePath, PlaceRuntimeIdentity& result);

inline bool writePlaceRuntimeIdentity(const std::string& placePath, long long placeId,
                                      long long universeId, long long creatorId, int creatorType)
{
    const unsigned long long bytes = fileSize(placePath);
    if (!bytes||placeId<0||universeId<0||creatorId<0||creatorType<0||creatorType>1)
        return false;

    PlaceRuntimeIdentity existing;
    if (fileExists(runtimeIdentityPath(placePath))&&readPlaceRuntimeIdentity(placePath, existing) &&
        existing.placeId == placeId && existing.universeId == universeId &&
        existing.creatorId == creatorId && existing.creatorType == creatorType)
        return true;

    std::ostringstream text;
    text << "version=2\n"
        << "file_size=" << bytes << '\n'
        << "place_id=" << placeId << '\n'
        << "universe_id=" << universeId << '\n'
        << "creator_id=" << creatorId << '\n'
        << "creator_type=" << creatorType << '\n';
    writeMetadataBinding(text,placePath);
    sceIoMkdir("ux0:/data/RBXV00001/system",0777);
    sceIoMkdir("ux0:/data/RBXV00001/system/game-info",0777);
    return writeAtomic(runtimeIdentityPath(placePath), text.str());
}

inline bool readPlaceRuntimeIdentity(const std::string& placePath, PlaceRuntimeIdentity& result)
{
    const auto preferred=runtimeIdentityPath(placePath);
    MetadataPolicy::Document document;if(!document.read(fileExists(preferred)?preferred:placePath+".vita-runtime",4096))return false;
    PlaceRuntimeIdentity parsed;unsigned version=0;
    if(!document.number("version",version,2,true)||version<1||!document.number("file_size",parsed.fileSize,~0ull,true)||
        !document.number("place_id",parsed.placeId,LLONG_MAX,true)||!document.number("universe_id",parsed.universeId,LLONG_MAX)||
        !document.number("creator_id",parsed.creatorId,LLONG_MAX)||!document.number("creator_type",parsed.creatorType,1))return false;

    const unsigned long long currentSize = fileSize(placePath);
    // Version 1 binds only to size. It cannot independently identify a copied
    // or replaced original; the optional archive worker must resolve its bytes.
    // Keep the old record untouched until a fresh native load writes version 2.
    if (version!=2 || !parsed.fileSize || !currentSize || parsed.fileSize != currentSize || !metadataBinding(document,placePath))
        return false;
    parsed.valid = true;
    result = parsed;
    return true;
}

inline bool writePublicPlaceMetadata(const std::string& placePath, const PublicPlaceMetadata& details)
{
    if (details.placeId <= 0 || details.universeId <= 0)
        return false;
    sceIoMkdir("ux0:/data/RBXV00001/system",0777);
    sceIoMkdir("ux0:/data/RBXV00001/system/game-info",0777);

    std::ostringstream text;
    text << "version=2\n"
        << "place_id=" << details.placeId << '\n'
        << "universe_id=" << details.universeId << '\n'
        << "name=" << metadataSingleLine(details.name) << '\n'
        << "creator=" << metadataSingleLine(details.creator) << '\n'
        << "year=" << metadataSingleLine(details.year) << '\n'
        << "description=" << metadataSingleLine(details.description) << '\n'
        << "description_hex=" << GameInfo::encode(details.description) << '\n'
        << "info_version=" << (details.info.ready?1:0) << '\n'
        << "info_available=" << details.info.available << '\n'
        << "info_fetched=" << (GameInfo::timestamp(details.info.fetchedAt)?details.info.fetchedAt:0) << '\n'
        << "info_created=" << GameInfo::date(details.info.created) << '\n'
        << "info_updated=" << GameInfo::date(details.info.updated) << '\n'
        << "info_genre=" << metadataSingleLine(details.info.genre) << '\n';
    for(unsigned i=0;i<GameInfo::CounterCount;++i)text<<"info_count_"<<i<<'='<<details.info.counts[i]<<'\n';
    writeMetadataBinding(text,placePath);
    return writeAtomic(publicMetadataPath(placePath,details.placeId), text.str());
}

inline bool readPublicPlaceMetadata(const std::string& placePath, const PlaceRuntimeIdentity& identity,
                                    PublicPlaceMetadata& result)
{
    const std::string preferred=publicMetadataPath(placePath,identity.placeId);
    const std::string oldCentral=publicMetadataPath(placePath);
    const std::string path=fileExists(preferred)?preferred:fileExists(oldCentral)?oldCentral:placePath+".vita-details";
    MetadataPolicy::Document document;if(!document.read(path,256u*1024u))return false;

    PublicPlaceMetadata parsed;
    unsigned decodedCounts=0;
    std::string multilineDescription;bool haveMultiline=false;
    unsigned version=0;
    if(!document.number("version",version,2,true)||version<1||!document.number("place_id",parsed.placeId,LLONG_MAX,true)||
        !document.number("universe_id",parsed.universeId,LLONG_MAX,true)||(version==2&&!metadataBinding(document,placePath)))return false;
    for(const auto& field:document.fields){const auto& key=field.first;const auto& value=field.second;
        if (key == "name") parsed.name = value;
        else if (key == "creator") parsed.creator = value;
        else if (key == "year") parsed.year = value;
        else if (key == "description") parsed.description = value;
        else if(key=="description_hex")haveMultiline=GameInfo::decode(value,multilineDescription);
        else if(key=="info_version")parsed.info.ready=value=="1";
        else if(key=="info_fetched"){unsigned long long stamp=0;if(GameInfo::number(value,stamp)&&GameInfo::timestamp(stamp))parsed.info.fetchedAt=stamp;}
        else if(key=="info_available"){unsigned long long mask=0;if(GameInfo::number(value,mask)&&mask<16)parsed.info.available=unsigned(mask);}
        else if(key=="info_created")parsed.info.created=GameInfo::date(value);
        else if(key=="info_updated")parsed.info.updated=GameInfo::date(value);
        else if(key=="info_genre")parsed.info.genre=value;
        else for(unsigned i=0;i<GameInfo::CounterCount;++i)if(key=="info_count_"+std::to_string(i)){
            if(GameInfo::number(value,parsed.info.counts[i]))decodedCounts|=1u<<i;
        }
    }

    if (parsed.placeId <= 0 || parsed.universeId <= 0 ||
        parsed.placeId != identity.placeId ||
        (identity.universeId > 0 && parsed.universeId != identity.universeId))
        return false;
    parsed.info.available&=decodedCounts;
    if(haveMultiline)parsed.description=multilineDescription;
    parsed.thumbnailPath = publicThumbnailPath(placePath,parsed.placeId);
    if (!fileExists(parsed.thumbnailPath)&&version==1) {
        const auto previous=publicThumbnailPath(placePath);
        const std::string legacy=placePath+".vita-thumbnail.png";
        parsed.thumbnailPath=fileExists(previous)?previous:fileExists(legacy)?legacy:std::string();
    }
    if(!fileExists(parsed.thumbnailPath))parsed.thumbnailPath.clear();
    parsed.valid = true;
    result = parsed;
    return true;
}

inline bool fetchPublicJson(const std::string& url, std::string& data, std::string& error)
{
#if defined(RBX_PLATFORM_PSVITA)
    RBX::VitaHttpRequestScope requestScope(64u * 1024u);
#endif

    data.clear();
    try
    {
#if defined(RBX_PLATFORM_PSVITA)
        RBX::Bloxicon::PublicArtworkScope artworkScope(url);
#endif
        RBX::Http request(url);
        request.shouldRetry = false;
        request.additionalHeaders["User-Agent"] = "Bloxicon-Vita/0.3";
        request.additionalHeaders["Accept"] = "application/json";
        request.setConnectionTimeout(5000);
        request.setResponseTimeout(10000);
        request.get(data, true);
        if (data.empty())
            throw std::runtime_error("Public metadata service returned an empty response");
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        if (error.size() > 110)
            error.resize(110);
        return false;
    }
}

inline bool trustedPublicThumbnailUrl(const std::string& url)
{
    static const std::string scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return false;
    const size_t end = url.find('/', scheme.size());
    const std::string host = lowerCopy(url.substr(scheme.size(), end == std::string::npos
        ? std::string::npos : end - scheme.size()));
    if (host.empty() || host.find('@') != std::string::npos || host.find(':') != std::string::npos)
        return false;
    static const std::string suffix = ".rbxcdn.com";
    return host == "rbxcdn.com" ||
        (host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0);
}

inline bool looksLikePngFile(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    unsigned char signature[8] = {};
    if (!input || !input.read(reinterpret_cast<char*>(signature), sizeof(signature)))
        return false;
    static const unsigned char pngSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    return std::memcmp(signature, pngSignature, sizeof(signature)) == 0;
}

inline bool parsePublicMetadataJson(const std::string& json, rapidjson::Document& document,
                                    std::string& error)
{
    if (json.empty() || json.size() > 64u * 1024u || json.find('\0') != std::string::npos)
    {
        error = "Bloxicon metadata response was empty or too large.";
        return false;
    }
    document.Parse<0>(json.c_str());
    if (document.HasParseError() || !document.IsObject())
    {
        error = "Bloxicon metadata response was invalid.";
        return false;
    }
    return true;
}

inline bool parsePublicUniverseMetadata(const std::string& json, long long& universeId,
                                        std::string& error)
{
    rapidjson::Document document;
    if (!parsePublicMetadataJson(json, document, error)) return false;
    if (!document.HasMember("universeId") || !document["universeId"].IsInt64() ||
        document["universeId"].GetInt64() <= 0)
    {
        error = "Bloxicon did not return a universe for this place.";
        return false;
    }
    universeId = document["universeId"].GetInt64();
    return true;
}

inline bool parsePublicGameMetadata(const std::string& json, long long placeId,
                                    long long universeId, PublicPlaceMetadata& result,
                                    std::string& error)
{
    rapidjson::Document document;
    if (!parsePublicMetadataJson(json, document, error)) return false;
    if (placeId <= 0 || universeId <= 0 || !document.HasMember("data") || !document["data"].IsArray())
    {
        error = "Bloxicon game metadata was invalid.";
        return false;
    }
    // Invalid IDs can be omitted, and batch ordering is not an identity check.
    for (rapidjson::SizeType i = 0; i < document["data"].Size(); ++i)
    {
        const rapidjson::Value& game = document["data"][i];
        if (!game.IsObject() || !game.HasMember("id") || !game["id"].IsInt64() ||
            game["id"].GetInt64() != universeId) continue;
        if(game.HasMember("isContentRestricted")&&(!game["isContentRestricted"].IsBool()||game["isContentRestricted"].GetBool())){
            error="Bloxicon restricts this game's online details. Saved info remains available.";return false;
        }
        PublicPlaceMetadata details;
        details.placeId = placeId;
        details.universeId = universeId;
        details.info.ready=true;
        const char* counters[]={"playing","visits","favoritedCount","maxPlayers"};
        for(unsigned n=0;n<GameInfo::CounterCount;++n)if(game.HasMember(counters[n])&&game[counters[n]].IsUint64()){
            details.info.counts[n]=game[counters[n]].GetUint64();details.info.available|=1u<<n;
        }
        if(game.HasMember("created")&&game["created"].IsString())details.info.created=GameInfo::date(game["created"].GetString());
        if(game.HasMember("updated")&&game["updated"].IsString())details.info.updated=GameInfo::date(game["updated"].GetString());
        if(game.HasMember("genre")&&game["genre"].IsString())details.info.genre=game["genre"].GetString();
        if (game.HasMember("name") && game["name"].IsString()) details.name = game["name"].GetString();
        if (game.HasMember("description") && game["description"].IsString())
            details.description = game["description"].GetString();
        if (game.HasMember("creator") && game["creator"].IsObject() &&
            game["creator"].HasMember("name") && game["creator"]["name"].IsString())
            details.creator = game["creator"]["name"].GetString();
        if (game.HasMember("created") && game["created"].IsString())
        {
            const std::string created = game["created"].GetString();
            if (created.size() >= 4 && std::isdigit(static_cast<unsigned char>(created[0])) &&
                std::isdigit(static_cast<unsigned char>(created[1])) &&
                std::isdigit(static_cast<unsigned char>(created[2])) &&
                std::isdigit(static_cast<unsigned char>(created[3]))) details.year = created.substr(0, 4);
        }
        details.valid = true;
        result = details;
        return true;
    }
    error = "Bloxicon did not return this game's metadata.";
    return false;
}

inline bool parsePublicThumbnailMetadata(const std::string& json, long long placeId,
                                         std::string& imageUrl, std::string& error)
{
    rapidjson::Document document;
    if (!parsePublicMetadataJson(json, document, error)) return false;
    if (placeId <= 0 || !document.HasMember("data") || !document["data"].IsArray())
    {
        error = "Bloxicon thumbnail metadata was invalid.";
        return false;
    }
    for (rapidjson::SizeType i = 0; i < document["data"].Size(); ++i)
    {
        const rapidjson::Value& entry = document["data"][i];
        if (!entry.IsObject() || !entry.HasMember("targetId") || !entry["targetId"].IsInt64() ||
            entry["targetId"].GetInt64() != placeId) continue;
        if (!entry.HasMember("state") || !entry["state"].IsString() ||
            std::string(entry["state"].GetString()) != "Completed" ||
            !entry.HasMember("imageUrl") || !entry["imageUrl"].IsString()) break;
        const std::string candidate(entry["imageUrl"].GetString(), entry["imageUrl"].GetStringLength());
        if (candidate.find('\0') != std::string::npos || !trustedPublicThumbnailUrl(candidate))
        {
            error = "Bloxicon thumbnail returned an untrusted image host.";
            return false;
        }
        imageUrl = candidate;
        return true;
    }
    error = "Bloxicon thumbnail is not available yet.";
    return false;
}

inline bool fetchPublicPlaceThumbnail(long long placeId, const std::string& placePath, std::string& error)
{
    std::ostringstream endpoint;
    endpoint << "https://thumbnails.roblox.com/v1/places/gameicons?placeIds=" << placeId
        << "&returnPolicy=PlaceHolder&size=512x512&format=Png&isCircular=false";
    std::string json;
    if (!fetchPublicJson(endpoint.str(), json, error))
        return false;

    std::string imageUrl;
    if (!parsePublicThumbnailMetadata(json, placeId, imageUrl, error)) return false;
    if (!trustedPublicThumbnailUrl(imageUrl))
    {
        error = "Bloxicon thumbnail returned an untrusted image host.";
        return false;
    }

    const std::string finalPath = publicThumbnailPath(placePath,placeId);
    const std::string tempPath = finalPath + ".download";
    size_t bytes = 0;
#if defined(RBX_PLATFORM_PSVITA)
    RBX::Bloxicon::PublicArtworkScope artworkScope(imageUrl);
#endif
    if (!fetchExternalToFile(imageUrl, tempPath, 2u * 1024u * 1024u, error, false, false, bytes))
        return false;
    if (!looksLikePngFile(tempPath))
    {
        std::remove(tempPath.c_str());
        error = "Bloxicon thumbnail response was not a PNG image.";
        return false;
    }
    if (!commitStagedFile(finalPath, tempPath))
    {
        std::remove(tempPath.c_str());
        error = "Could not cache the Bloxicon thumbnail.";
        return false;
    }
    return true;
}

inline bool fetchPublicPlaceMetadata(const std::string& placePath, const PlaceRuntimeIdentity& identity,
                                     PublicPlaceMetadata& result, std::string& error)
{
    if (identity.placeId <= 0)
    {
        error = "This archived place does not contain a public Bloxicon PlaceId.";
        return false;
    }

    long long universeId = identity.universeId;
    if (universeId <= 0)
    {
        std::ostringstream endpoint;
        endpoint << "https://apis.roblox.com/universes/v1/places/" << identity.placeId << "/universe";
        std::string json;
        if (!fetchPublicJson(endpoint.str(), json, error))
            return false;
        if (!parsePublicUniverseMetadata(json, universeId, error)) return false;
    }
    if (universeId <= 0)
    {
        error = "Bloxicon did not return a universe for this place.";
        return false;
    }

    std::ostringstream endpoint;
    endpoint << "https://games.roblox.com/v1/games?universeIds=" << universeId;
    std::string json;
    if (!fetchPublicJson(endpoint.str(), json, error))
        return false;

    PublicPlaceMetadata details;
    if (!parsePublicGameMetadata(json, identity.placeId, universeId, details, error)) return false;
    if (!writePublicPlaceMetadata(placePath, details))
    {
        error = "Could not cache public Bloxicon game metadata.";
        return false;
    }

    std::string thumbnailError;
    fetchPublicPlaceThumbnail(identity.placeId, placePath, thumbnailError);
    details.thumbnailPath = publicThumbnailPath(placePath,identity.placeId);
    if (!fileExists(details.thumbnailPath))
        details.thumbnailPath.clear();
    result = details;
    return true;
}

inline void clearPlaceDetailCaches(const std::string& placePath)
{
    MetadataPolicy::Document previous;long long id=0;
    if(previous.read(publicMetadataPath(placePath),256u*1024u)&&previous.number("place_id",id,LLONG_MAX)&&id>0)
        std::remove(publicThumbnailPath(placePath,id).c_str());
    const std::string files[] = {
        runtimeIdentityPath(placePath), placePath+".vita-runtime", publicMetadataPath(placePath), placePath+".vita-details", publicThumbnailPath(placePath)
    };
    for (const std::string& path : files)
    {
        std::remove(path.c_str());
        std::remove((path + ".download").c_str());
        std::remove((path + ".previous").c_str());
    }
}

inline std::string catalogStableSource(const std::string& url,const std::string& name)
{
    const std::string raw="https://raw.githubusercontent.com/";
    if(url.compare(0,raw.size(),raw)==0) {
        const auto ownerEnd=url.find('/',raw.size());
        const auto repoEnd=ownerEnd==std::string::npos?ownerEnd:url.find('/',ownerEnd+1);
        const auto revisionEnd=repoEnd==std::string::npos?repoEnd:url.find('/',repoEnd+1);
        if(revisionEnd!=std::string::npos)return url.substr(0,repoEnd)+url.substr(revisionEnd);
    }
    const std::string api="https://api.github.com/repos/";
    const auto blob=url.find("/git/blobs/");
    if(url.compare(0,api.size(),api)==0&&blob!=std::string::npos)return url.substr(0,blob)+"/"+name;
    return url;
}

inline std::string catalogInstallPath(const CatalogItem& item)
{
    const std::string legacy="ux0:/data/RBXV00001/places/" + safeFileName(item.name);
    InstalledPlaceMetadata old;
    if(fileExists(legacy) && readInstallMetadata(legacy,old) && catalogStableSource(old.item.url,old.item.name)==catalogStableSource(item.url,item.name)) return legacy;
    uint64_t hash=1469598103934665603ULL;
    for(unsigned char c:catalogStableSource(item.url,item.name)) { hash^=c;hash*=1099511628211ULL; }
    std::ostringstream key;key<<std::hex<<hash;
    std::string folder=safeFileName(item.name);
    const auto dot=folder.find_last_of('.');if(dot!=std::string::npos)folder.resize(dot);
    if(folder.size()>72)folder.resize(72);
    return "ux0:/data/RBXV00001/places/"+folder+"-"+key.str()+"/"+safeFileName(item.name);
}

inline size_t catalogRetainedDownloadBytes(const CatalogItem& item)
{
    const std::string tempPath=catalogInstallPath(item)+".download";
    if (!downloadResumeMetadataMatches(tempPath,item))
        return 0;
    const unsigned long long bytes=fileSize(tempPath);
    return bytes<=48ull*1024ull*1024ull?static_cast<size_t>(bytes):0;
}

enum CatalogInstallState
{
    CatalogNotInstalled,
    CatalogInstalledUnknown,
    CatalogInstalledCurrent,
    CatalogUpdateAvailable
};

inline CatalogInstallState catalogInstallState(const CatalogItem& item)
{
    const std::string path = catalogInstallPath(item);
    if (!fileExists(path))
        return CatalogNotInstalled;

    InstalledPlaceMetadata metadata;
    if (!readInstallMetadata(path, metadata) ||
        catalogStableSource(metadata.item.url,metadata.item.name)!=catalogStableSource(item.url,item.name))
        return CatalogInstalledUnknown;
    if(metadata.item.url!=item.url)return CatalogUpdateAvailable;

    const unsigned long long actual = fileSize(path);
    if ((metadata.installedSize && actual && metadata.installedSize != actual) ||
        (item.size && actual && item.size != actual))
        return CatalogUpdateAvailable;
    return CatalogInstalledCurrent;
}

inline const CatalogItem* findCatalogItemForInstalled(const std::string& placePath,
                                                       const std::string& placeName,
                                                       const std::vector<CatalogItem>& catalog)
{
    InstalledPlaceMetadata metadata;
    if (readInstallMetadata(placePath, metadata))
        for (const CatalogItem& item : catalog)
            if (catalogStableSource(item.url,item.name) == catalogStableSource(metadata.item.url,metadata.item.name))
                return &item;

    // A filename is not an identity. Manually copied/renamed files are matched
    // to the archive's exact blob hash by the optional Details worker.
    (void)placeName;
    return NULL;
}

inline bool isUserInstalledPlacePath(const std::string& path)
{
    static const std::string root = "ux0:/data/RBXV00001/places/";
    if(path.compare(0,root.size(),root)!=0)return false;
    const auto relative=path.substr(root.size());
    if(relative.empty()||relative.find_first_of("\\:\0",0,3)!=std::string::npos)return false;
    size_t start=0;
    for(;;){const auto end=relative.find('/',start);const auto part=relative.substr(start,end-start);
        if(part.empty()||part=="."||part=="..")return false;
        if(end==std::string::npos)break;start=end+1;}
    return true;
}

inline bool removeInstalledPlace(const std::string& path, std::string& status)
{
    if (!isUserInstalledPlacePath(path))
    {
        status = "Built-in games cannot be removed from the loader.";
        return false;
    }
    if (!fileExists(path))
    {
        status = "Game file is already missing.";
        return false;
    }
    if (std::remove(path.c_str()) != 0)
    {
        status = "Could not remove the game file.";
        return false;
    }

    const std::string metadata = installMetadataPath(path);
    std::remove(metadata.c_str());
    std::remove((metadata + ".download").c_str());
    std::remove((metadata + ".previous").c_str());
    VitaPlaceCache::removeForSource(path);
    clearPlaceDetailCaches(path);
    status = "Game removed.";
    return true;
}

inline std::vector<std::string> readNameList(const char* path)
{
    std::vector<std::string> result;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line))
        if (!line.empty())
            result.push_back(line);
    return result;
}

inline bool nameListContains(const std::vector<std::string>& values, const std::string& name)
{
    const std::string key = lowerCopy(name);
    for (const std::string& value : values)
        if (lowerCopy(value) == key)
            return true;
    return false;
}

inline bool writeNameList(const char* path, const std::vector<std::string>& values)
{
    std::ostringstream text;
    for (const std::string& value : values)
        text << metadataValue(value) << '\n';
    return writeAtomic(path, text.str());
}

inline bool isFavorite(const std::string& name)
{
    return nameListContains(readNameList("ux0:/data/RBXV00001/system/favorites.txt"), name);
}

inline bool toggleFavorite(const std::string& name)
{
    std::vector<std::string> values = readNameList("ux0:/data/RBXV00001/system/favorites.txt");
    const std::string key = lowerCopy(name);
    bool removed = false;
    for (std::vector<std::string>::iterator it = values.begin(); it != values.end(); ++it)
        if (lowerCopy(*it) == key)
        {
            values.erase(it);
            removed = true;
            break;
        }
    if (!removed)
        values.insert(values.begin(), name);
    writeNameList("ux0:/data/RBXV00001/system/favorites.txt", values);
    return !removed;
}

inline void noteRecent(const std::string& name)
{
    std::vector<std::string> values = readNameList("ux0:/data/RBXV00001/system/recent-games.txt");
    const std::string key = lowerCopy(name);
    values.erase(std::remove_if(values.begin(), values.end(), [&](const std::string& value) {
        return lowerCopy(value) == key;
    }), values.end());
    values.insert(values.begin(), name);
    if (values.size() > 12)
        values.resize(12);
    writeNameList("ux0:/data/RBXV00001/system/recent-games.txt", values);
}

inline bool isRecent(const std::string& name)
{
    return nameListContains(readNameList("ux0:/data/RBXV00001/system/recent-games.txt"), name);
}

using CatalogNameIndex=std::map<std::string,size_t>;
inline std::string catalogEntryKey(const CatalogItem& item){
    return item.url.empty()?"name:"+lowerCopy(item.name):"source:"+catalogStableSource(item.url,item.name);
}
inline void appendUniqueCatalogItem(std::vector<CatalogItem>& out, const CatalogItem& item,
                                    CatalogNameIndex* index=NULL)
{
    const std::string key=catalogEntryKey(item);
    size_t match=out.size();
    if(index) {
        const auto found=index->find(key);
        if(found!=index->end()) match=found->second;
    } else {
        for(size_t i=0;i<out.size();++i)
            if(catalogEntryKey(out[i])==key) {match=i;break;}
    }
    if(match<out.size()) {
        CatalogItem& existing=out[match];
            // Featured/seed rows are inserted before live provider metadata.
            // Enrich them with exact provider size/origin rather than keeping
            // an unversioned duplicate that can never detect updates.
            if (!existing.size && item.size)
                existing.size = item.size;
            if (existing.url.empty() && !item.url.empty())
                existing.url = item.url;
            if ((existing.source.empty() || existing.source == "FEATURED" || existing.source == "SEED") &&
                !item.source.empty() && item.source != "FEATURED" && item.source != "SEED")
                existing.source = item.source;
            if (!existing.githubRawApi && item.githubRawApi && existing.url == item.url)
                existing.githubRawApi = true;
            if (!existing.placeId && item.placeId)
                existing.placeId = item.placeId;
            if (existing.year.empty() && !item.year.empty())
                existing.year = item.year;
            if (existing.creator.empty() && !item.creator.empty())
                existing.creator = item.creator;
            if (existing.description.empty() && !item.description.empty())
                existing.description = item.description;
            if (existing.thumbnailUrl.empty() && !item.thumbnailUrl.empty())
                existing.thumbnailUrl = item.thumbnailUrl;
            if(existing.contentHash.empty() && !item.contentHash.empty() &&
                catalogStableSource(existing.url,existing.name)==catalogStableSource(item.url,item.name)){
                existing.contentHash=item.contentHash;
                // Keep the checksum and its exact revision together rather
                // than downloading a mutable branch with a pinned checksum.
                existing.url=item.url;existing.githubRawApi=item.githubRawApi;
            }
        return;
    }
    if(index) index->emplace(key,out.size());
    out.push_back(item);
}

inline void appendBundledCatalog(std::vector<CatalogItem>& out,CatalogNameIndex* index=NULL)
{
    std::ifstream input("ux0:/data/RBXV00001/system/catalog/places.v2",std::ios::binary);
    if(!input.is_open()){input.clear();input.open("app0:/catalog/places.v2",std::ios::binary);}
    std::string line;std::getline(input,line);
    if(line!="RBXV_PLACE_INDEX\t2")return;
    size_t bytes=line.size(),count=0;
    while(count<8192 && std::getline(input,line)) {
        bytes+=line.size();if(bytes>4u*1024u*1024u||line.size()>4096)break;
        std::vector<std::string> fields;size_t start=0;
        for(size_t i=0;i<=line.size();++i)if(i==line.size()||line[i]=='\t'){fields.push_back(line.substr(start,i-start));start=i+1;}
        if(fields.size()!=7||!supportedPlaceName(fields[0])||fields[1].compare(0,34,"https://raw.githubusercontent.com/")!=0)continue;
        unsigned long long size=0;std::istringstream number(fields[2]);number>>size;
        if(!size||size>48ull*1024ull*1024ull)continue;
        CatalogItem item(fields[0],fields[1],size,false,fields[3]);
        if(ArchiveIdentity::validHash(fields[6]))item.contentHash=fields[6];
        item.year=fields[4];item.creator=fields[5];item.description="Public archive index. Runtime compatibility has not been verified for this place.";
        appendUniqueCatalogItem(out,item,index);++count;
    }
}

inline void appendSeedCatalog(std::vector<CatalogItem>& out, CatalogNameIndex* index=NULL)
{
    // The seed is ordinary updateable data rather than VPK content. It lets
    // the Vita browse a large known-safe public archive immediately even when
    // GitHub's metadata API is rate-limited/offline; the place bytes still
    // download directly on the Vita when the user presses Install.
    std::ifstream input("ux0:/data/RBXV00001/system/catalog-seed.txt");
    std::string line;
    while (std::getline(input, line))
    {
        const size_t first = line.find('|');
        const size_t second = first == std::string::npos ? std::string::npos : line.find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            continue;

        const std::string name = line.substr(0, first);
        const std::string url = line.substr(first + 1, second - first - 1);
        if (!supportedPlaceName(name) || url.compare(0, 8, "https://") != 0)
            continue;

        unsigned long long size = 0;
        std::istringstream sizeStream(line.substr(second + 1));
        sizeStream >> size;
        if (size > 48ull * 1024ull * 1024ull)
            continue;

        appendUniqueCatalogItem(out, CatalogItem(name, url, size, false, "SEED"), index);
    }
}

inline void appendFeaturedCatalog(std::vector<CatalogItem>& out, CatalogNameIndex* index=NULL)
{
    struct Featured { const char* name; const char* url; };
    static const Featured featured[] = {
        {"Crossroads.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Crossroads.rbxl"},
        {"Happy Home in Robloxia.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Happy%20Home%20in%20Robloxia.rbxl"},
        {"Chaos Canyon.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Chaos%20Canyon.rbxl"},
        {"DoomSpire Tower Battle.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/DoomSpire%20Tower%20Battle.rbxl"},
        {"Be crushed by speeding wall.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Be%20crushed%20by%20speeding%20wall.rbxl"},
        {"Glass Houses.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Glass%20Houses.rbxl"},
        {"Haunted Mansion.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Haunted%20Mansion.rbxl"},
        {"Four Towers Bridge Battle.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Four%20Towers%20Bridge%20Battle.rbxl"},
        {"Natural Disaster Survival.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Natural%20Disaster%20Survival.rbxl"},
        {"Base Wars.rbxl", "https://raw.githubusercontent.com/beagleded/Roblox-Places-Archive/main/Games/Base%20Wars.rbxl"}
    };

    for (const Featured& item : featured)
        appendUniqueCatalogItem(out, CatalogItem(item.name, item.url, 0, true, "FEATURED"), index);
}

inline bool parseGithubCatalog(const std::string& json, std::vector<CatalogItem>& out,
                               std::string& error, const std::string& source, CatalogNameIndex* index=NULL)
{
    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    if (document.HasParseError() || !document.IsArray())
    {
        error = "The online catalog returned invalid JSON.";
        return false;
    }

    std::vector<CatalogItem> discovered;
    std::set<std::string> seenBlobs;
    for (rapidjson::Value::ConstValueIterator it = document.Begin(); it != document.End(); ++it)
    {
        if (!it->IsObject())
            continue;
        const rapidjson::Value& item = *it;
        if (!item.HasMember("type") || !item["type"].IsString() ||
            std::string(item["type"].GetString()) != "file")
            continue;
        if (!item.HasMember("name") || !item["name"].IsString() ||
            !item.HasMember("download_url") || !item["download_url"].IsString())
            continue;

        std::string name = item["name"].GetString();
        if (!supportedPlaceName(name))
            continue;

        unsigned long long size = 0;
        if (item.HasMember("size") && item["size"].IsUint64())
            size = item["size"].GetUint64();
        if (size > 48ull * 1024ull * 1024ull)
            continue;
        if (item.HasMember("sha") && item["sha"].IsString())
        {
            const std::string sha = item["sha"].GetString();
            if (!sha.empty() && !seenBlobs.insert(sha).second)
                continue; // Ignore duplicate "(2)" mirrors of the same place blob.
        }

        discovered.push_back(CatalogItem(name, item["download_url"].GetString(), size, false, source));
        if(item.HasMember("sha")&&item["sha"].IsString()&&ArchiveIdentity::validHash(item["sha"].GetString()))
            discovered.back().contentHash=item["sha"].GetString();
    }

    std::sort(discovered.begin(), discovered.end(), [](const CatalogItem& a, const CatalogItem& b) {
        return lowerCopy(a.name) < lowerCopy(b.name);
    });

    for (const CatalogItem& item : discovered)
    {
        appendUniqueCatalogItem(out, item, index);
    }
    return true;
}

inline std::string githubTreeDisplayName(std::string path)
{
    for (size_t pos = 0; (pos = path.find('/', pos)) != std::string::npos; )
    {
        path.replace(pos, 1, " - ");
        pos += 3;
    }
    return path;
}

inline std::string trimCopy(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;
    return value.substr(first, last - first);
}

inline std::string htmlText(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    bool tag = false;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '<')
        {
            tag = true;
            continue;
        }
        if (tag)
        {
            if (value[i] == '>')
                tag = false;
            continue;
        }
        if (value.compare(i, 5, "&amp;") == 0)
        {
            out.push_back('&');
            i += 4;
        }
        else if (value.compare(i, 6, "&quot;") == 0)
        {
            out.push_back('"');
            i += 5;
        }
        else if (value.compare(i, 5, "&#39;") == 0)
        {
            out.push_back('\'');
            i += 4;
        }
        else
            out.push_back(value[i]);
    }
    return trimCopy(out);
}

inline std::string catalogIdentityKey(std::string value)
{
    value = lowerCopy(value);
    const size_t dot = value.find_last_of('.');
    if (dot != std::string::npos && (value.substr(dot)==".rbxl"||value.substr(dot)==".rbxlx"))
        value.resize(dot);
    for (size_t pos = 0; (pos = value.find("version", pos)) != std::string::npos; )
    {
        size_t next=pos+7;while(next<value.size()&&std::isspace(static_cast<unsigned char>(value[next])))++next;
        if((pos==0||!std::isalnum(static_cast<unsigned char>(value[pos-1])))&&next<value.size()&&std::isdigit(static_cast<unsigned char>(value[next])))
            value.replace(pos,7,"v");
        ++pos;
    }
    std::string key;
    for (char c : value)
        if (std::isalnum(static_cast<unsigned char>(c))||static_cast<unsigned char>(c)>=128)
            key.push_back(c);
    return key;
}

inline std::string yearFromText(const std::string& value)
{
    for (size_t i = 0; i + 4 <= value.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[i])) ||
            !std::isdigit(static_cast<unsigned char>(value[i + 1])) ||
            !std::isdigit(static_cast<unsigned char>(value[i + 2])) ||
            !std::isdigit(static_cast<unsigned char>(value[i + 3])))
            continue;
        const int year = std::atoi(value.substr(i, 4).c_str());
        if (year >= 2006 && year <= 2099)
            return value.substr(i, 4);
    }
    return std::string();
}

inline std::string officialArchiveCreator(const std::string& path)
{
    static const std::string prefix = "Places/";
    if (path.compare(0, prefix.size(), prefix) != 0)
        return std::string();
    const size_t slash = path.find('/', prefix.size());
    if (slash == std::string::npos)
        return std::string();
    const std::string owner = path.substr(prefix.size(), slash - prefix.size());
    return lowerCopy(owner) == "games" ? std::string() : owner;
}

inline bool applyOfficialArchiveMetadata(const std::string& listText,
                                         std::vector<CatalogItem>& items,
                                         std::string& error)
{
    if(listText.size()>1024u*1024u||listText.find('\0')!=std::string::npos){error="Archive metadata exceeds the supported bounds.";return false;}
    struct Record
    {
        std::string title;
        unsigned long long placeId;
        std::string year;
        std::string description;
        Record(): placeId(0) {}
    };

    std::vector<Record> records;
    Record current;
    bool active = false;
    std::istringstream stream(listText);
    std::string line;
    while (std::getline(stream, line))
    {
        const std::string text = htmlText(line);
        const size_t idMarker = text.find(" | ID ");
        if (idMarker != std::string::npos)
        {
            if (active && current.placeId && !current.title.empty()){
                if(records.size()==8192){error="Archive metadata contains too many entries.";return false;}
                records.push_back(current);
            }
            current = Record();
            active = true;
            current.title = trimCopy(text.substr(0, idMarker));
            const size_t idStart=idMarker+6,dateMarker=text.find(" | ",idStart);
            std::string idText=text.substr(idStart,dateMarker==std::string::npos?std::string::npos:dateMarker-idStart);
            const auto suffix=idText.find('&');
            bool validSuffix=true;
            if(suffix!=std::string::npos){unsigned long long version=0;
                validSuffix=idText.compare(suffix,9,"&version=")==0&&GameInfo::number(idText.substr(suffix+9),version)&&version>0;
                idText.resize(suffix);}
            if(!validSuffix||!GameInfo::number(idText,current.placeId)||current.placeId==0||current.placeId>static_cast<unsigned long long>(LLONG_MAX)){
                error="Archive metadata contains an invalid place ID.";return false;}
            if (dateMarker != std::string::npos)
                current.year = yearFromText(text.substr(dateMarker + 3));
            continue;
        }
        if (active && !text.empty() && text[0] != '#')
        {
            if (!current.description.empty())
                current.description += " ";
            current.description += text;
        }
    }
    if (active && current.placeId && !current.title.empty()){
        if(records.size()==8192){error="Archive metadata contains too many entries.";return false;}
        records.push_back(current);
    }
    if (records.empty())
    {
        error = "The official place archive metadata list was invalid.";
        return false;
    }

    // Normalize each metadata title once instead of once per candidate match.
    std::map<std::string,const Record*> recordIndex;
    for(const Record& record:records){
        const auto key=catalogIdentityKey(record.title);if(key.empty())continue;
        const auto inserted=recordIndex.emplace(key,&record);
        if(!inserted.second&&(!inserted.first->second||inserted.first->second->placeId!=record.placeId))
            inserted.first->second=nullptr;
    }
    unsigned matched = 0;
    for (CatalogItem& item : items)
    {
        if (item.source != "HISTORICAL PLACE ARCHIVE") continue;
        const auto key=catalogIdentityKey(item.name);if(key.empty())continue;
        const auto found=recordIndex.find(key);
        if(found==recordIndex.end()||!found->second) continue;
        const Record& record=*found->second;
        item.placeId=record.placeId;item.year=record.year;item.description=record.description;
        ++matched;
    }
    if (!matched)
    {
        error = "The official place archive metadata did not match its place files.";
        return false;
    }
    return true;
}

inline bool trustedPlaceArtworkUrl(const std::string& url)
{
    const std::string host=CatalogDownloadPolicy::poolOrigin();
    if(url.compare(0,host.size(),host))return false;
    if(hostedArtworkPath(url.substr(host.size())))return true;
    const std::string prefix=std::string(CatalogDownloadPolicy::poolOrigin())+"/api/v1/catalogue/";
    if(url.compare(0,prefix.size(),prefix)||url.find_first_of("\\\r\n\t #")!=std::string::npos)return false;
    const auto end=url.find('/',prefix.size());
    if(end==std::string::npos)return false;
    const auto id=url.substr(prefix.size(),end-prefix.size());
    if(id.size()<8||id.size()>32||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)return false;
    const auto query=url.find('?',end);
    return url.substr(end,query==std::string::npos?query:query-end)=="/art";
}
// Kept as a source-compatible name for old cache parsers. Only Bloxicon art
// qualifies; the central network policy also rejects every Roblox origin.
inline bool trustedRobloxThumbnailUrl(const std::string& url){return trustedPlaceArtworkUrl(url);}

inline bool applyOfficialPlaceIcons(const std::string& json, std::vector<CatalogItem>& items,
                                    std::string& error)
{
    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    if (document.HasParseError() || !document.IsObject() ||
        !document.HasMember("data") || !document["data"].IsArray())
    {
        error = "The Bloxicon thumbnail service returned invalid JSON.";
        return false;
    }

    unsigned matched = 0;
    for (rapidjson::Value::ConstValueIterator it = document["data"].Begin();
         it != document["data"].End(); ++it)
    {
        if (!it->IsObject() || !it->HasMember("targetId") ||
            !(*it)["targetId"].IsUint64() || !it->HasMember("state") ||
            !(*it)["state"].IsString() || std::string((*it)["state"].GetString()) != "Completed" ||
            !it->HasMember("imageUrl") || !(*it)["imageUrl"].IsString())
            continue;
        const unsigned long long targetId = (*it)["targetId"].GetUint64();
        const std::string imageUrl = (*it)["imageUrl"].GetString();
        if (!trustedRobloxThumbnailUrl(imageUrl))
            continue;
        for (CatalogItem& item : items)
            if (item.placeId == targetId)
            {
                item.thumbnailUrl = imageUrl;
                ++matched;
            }
    }
    if (!matched)
    {
        error = "No completed Bloxicon thumbnails matched the archive place IDs.";
        return false;
    }
    return true;
}

inline bool parseGithubTreeCatalog(const std::string& json, std::vector<CatalogItem>& out,
                                   std::string& error, const std::string& source, CatalogNameIndex* index=NULL,
                                   const std::string& treeRequestUrl=std::string())
{
    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    if (document.HasParseError() || !document.IsObject() ||
        !document.HasMember("tree") || !document["tree"].IsArray())
    {
        error = "The online catalog tree returned invalid JSON.";
        return false;
    }
    if (document.HasMember("truncated") && document["truncated"].IsBool() &&
        document["truncated"].GetBool())
    {
        error = "The online catalog tree was truncated.";
        return false;
    }

    std::vector<CatalogItem> discovered;
    std::set<std::string> seenBlobs;
    const rapidjson::Value& tree = document["tree"];
    for (rapidjson::Value::ConstValueIterator it = tree.Begin(); it != tree.End(); ++it)
    {
        if (!it->IsObject())
            continue;
        const rapidjson::Value& item = *it;
        if (!item.HasMember("type") || !item["type"].IsString() ||
            std::string(item["type"].GetString()) != "blob")
            continue;
        if (!item.HasMember("path") || !item["path"].IsString() ||
            !item.HasMember("url") || !item["url"].IsString())
            continue;

        const std::string path = item["path"].GetString();
        if (!supportedPlaceName(path))
            continue;

        unsigned long long size = 0;
        if (item.HasMember("size") && item["size"].IsUint64())
            size = item["size"].GetUint64();
        if (size > 48ull * 1024ull * 1024ull)
            continue;
        if (item.HasMember("sha") && item["sha"].IsString())
        {
            const std::string sha = item["sha"].GetString();
            if (!sha.empty() && !seenBlobs.insert(sha).second)
                continue;
        }

        CatalogItem entry(
            source == "HISTORICAL PLACE ARCHIVE" ? safeFileName(path) : githubTreeDisplayName(path),
            item["url"].GetString(), size, false, source, true);
        if(item.HasMember("sha")&&item["sha"].IsString()&&ArchiveIdentity::validHash(item["sha"].GetString())) {
            entry.contentHash=item["sha"].GetString();
            const auto raw=CatalogDownloadPolicy::rawFromTree(treeRequestUrl,path);
            if(!raw.empty()){entry.url=raw;entry.githubRawApi=false;}
        }
        if (source == "HISTORICAL PLACE ARCHIVE")
            entry.creator = officialArchiveCreator(path);
        discovered.push_back(entry);
    }

    std::sort(discovered.begin(), discovered.end(), [](const CatalogItem& a, const CatalogItem& b) {
        return lowerCopy(a.name) < lowerCopy(b.name);
    });
    for (const CatalogItem& item : discovered)
        appendUniqueCatalogItem(out, item, index);
    return true;
}

inline bool readCatalogCache(const std::string& path,std::string& data,size_t limit=8u*1024u*1024u)
{
    std::ifstream cached(path.c_str(),std::ios::binary|std::ios::ate);
    const auto size=cached?std::streamoff(cached.tellg()):std::streamoff(-1);
    if(size<=0||static_cast<unsigned long long>(size)>limit)return false;
    std::string candidate(static_cast<size_t>(size),'\0');cached.seekg(0);
    cached.read(&candidate[0],size);
    if(!cached||cached.peek()!=std::char_traits<char>::eof()||cached.bad())return false;
    data.swap(candidate);return true;
}

struct CatalogRequestPolicy
{
    bool cacheOnly = false;
    const std::atomic<bool>* cancelled = nullptr;
    bool stopped() const { return cancelled && cancelled->load(std::memory_order_acquire); }
    bool mayFetch() const { return !cacheOnly && !stopped(); }
};

inline bool enrichOfficialArchiveMetadata(std::vector<CatalogItem>& items, bool forceRefresh,
                                          std::string& error,
                                          const CatalogRequestPolicy& policy = CatalogRequestPolicy())
{
    if(policy.stopped())return false;
    static const char* url =
        "https://raw.githubusercontent.com/tropicalbananas/robloxplacearchive/main/LIST.md";
    static const char* cache =
        "ux0:/data/RBXV00001/system/catalog-cache-official-place-archive-list.md";
    static const char* builtin="ux0:/data/RBXV00001/system/catalog/historical-ids.md";
    std::string cached;
    const bool haveCache = fileSize(cache)<=1024u*1024u&&readCatalogCache(cache, cached);

    if ((!forceRefresh || policy.cacheOnly) && haveCache)
    {
        std::string parseError;
        if (applyOfficialArchiveMetadata(cached, items, parseError))
            return true;
        error = parseError;
    }

    std::string bundled;
    const bool haveBuiltin=fileSize(builtin)<=1024u*1024u&&readCatalogCache(builtin,bundled);
    if((!forceRefresh||policy.cacheOnly)&&haveBuiltin){
        std::string parseError;if(applyOfficialArchiveMetadata(bundled,items,parseError)){error.clear();return true;}
    }

    std::string fresh;
    std::string fetchError;
    if (policy.mayFetch() && fetchExternal(url, fresh, fetchError) && !policy.stopped())
    {
        std::string parseError;
        if (applyOfficialArchiveMetadata(fresh, items, parseError))
        {
            writeAtomic(cache, fresh);
            error.clear();
            return true;
        }
        error = parseError;
    }
    else
        error = fetchError;

    if (haveCache)
    {
        std::string parseError;
        if (applyOfficialArchiveMetadata(cached, items, parseError))
            return true;
        if (error.empty())
            error = parseError;
    }
    if(haveBuiltin){std::string parseError;if(applyOfficialArchiveMetadata(bundled,items,parseError))return true;
        if(error.empty())error=parseError;}
    return false;
}

inline std::string officialPlaceIconRequest(const std::vector<CatalogItem>& items)
{
    std::ostringstream ids;
    bool first = true;
    for (const CatalogItem& item : items)
    {
        if (!item.placeId)
            continue;
        if (!first)
            ids << ',';
        ids << item.placeId;
        first = false;
    }
    if (first)
        return std::string();
    return "https://thumbnails.roblox.com/v1/places/gameicons?placeIds=" + ids.str() +
        "&returnPolicy=PlaceHolder&size=512x512&format=Png&isCircular=false";
}

inline bool enrichOfficialPlaceIcons(std::vector<CatalogItem>& items, bool forceRefresh,
                                     std::string& error,
                                     const CatalogRequestPolicy& policy = CatalogRequestPolicy())
{
    const std::string requestUrl = officialPlaceIconRequest(items);
    if (requestUrl.empty())
        return true;
#if defined(RBX_PLATFORM_PSVITA)
    RBX::Bloxicon::PublicArtworkScope artworkScope(requestUrl);
#endif

    static const char* cache =
        "ux0:/data/RBXV00001/system/catalog-cache-official-place-icons.json";
    std::string cached;
    const bool haveCache = readCatalogCache(cache, cached);
    if ((!forceRefresh || policy.cacheOnly) && haveCache)
    {
        std::string parseError;
        if (applyOfficialPlaceIcons(cached, items, parseError))
            return true;
        error = parseError;
    }

    std::string fresh;
    std::string fetchError;
    if (policy.mayFetch() && fetchExternal(requestUrl, fresh, fetchError) && !policy.stopped())
    {
        std::string parseError;
        if (applyOfficialPlaceIcons(fresh, items, parseError))
        {
            writeAtomic(cache, fresh);
            error.clear();
            return true;
        }
        error = parseError;
    }
    else
        error = fetchError;

    if (haveCache)
    {
        std::string parseError;
        if (applyOfficialPlaceIcons(cached, items, parseError))
            return true;
        if (error.empty())
            error = parseError;
    }
    return false;
}

inline std::string thumbnailCachePath(const CatalogItem& item)
{
    if(CatalogDownloadPolicy::ownedObject(item.url))
        return "ux0:/data/RBXV00001/system/thumbnails/hosted-"+item.url.substr(item.url.size()-64)+"-"+artworkKey(item.thumbnailUrl)+".image";
    return item.thumbnailUrl.empty()?std::string():"ux0:/data/RBXV00001/system/thumbnails/hosted-"+artworkKey(item.url+"\n"+item.thumbnailUrl)+".image";
}
inline bool validCachedPng(const std::string& path)
{
    const auto bytes=fileSize(path);
    if(bytes<8||bytes>2ull*1024ull*1024ull)return false;
    std::ifstream in(path.c_str(),std::ios::binary);unsigned char signature[8]={};in.read(reinterpret_cast<char*>(signature),8);
    static const unsigned char png[8]={137,80,78,71,13,10,26,10};
    return in.gcount()==8 && (std::memcmp(signature,png,8)==0 || (signature[0]==255&&signature[1]==216&&signature[2]==255));
}

inline bool ensureThumbnailCached(const CatalogItem& item, std::string& path, std::string& error)
{
    path = thumbnailCachePath(item);
    if (path.empty() || item.thumbnailUrl.empty() || !trustedRobloxThumbnailUrl(item.thumbnailUrl))
        return false;
    if (validCachedPng(path))
        return true;

    sceIoMkdir("ux0:/data/RBXV00001", 0777);
    sceIoMkdir("ux0:/data/RBXV00001/system/thumbnails", 0777);
    const std::string staged = path + ".download";
    size_t bytes = 0;
#if defined(RBX_PLATFORM_PSVITA)
    RBX::Bloxicon::PublicArtworkScope artworkScope(item.thumbnailUrl);
#endif
    if (!fetchExternalToFile(item.thumbnailUrl, staged, 2u * 1024u * 1024u,
                             error, false, false, bytes, 15000))
    {
        std::remove(staged.c_str());
        return false;
    }
    if (!validCachedPng(staged))
    {
        std::remove(staged.c_str());
        error = "Bloxicon returned an invalid thumbnail image.";
        return false;
    }
    if (!commitStagedFile(path, staged))
    {
        std::remove(staged.c_str());
        error = "Could not save the thumbnail cache.";
        return false;
    }
    return true;
}

inline CatalogDownloadPolicy::ProviderHealth catalogProviderHealth()
{
    std::ifstream input("ux0:/data/RBXV00001/system/provider-health.v1");
    if(!input){input.clear();input.open("ux0:/data/RBXV00001/system/catalog/provider-health.v1");}
    return CatalogDownloadPolicy::readProviderHealth(input);
}

inline void enrichOwnedCatalogue(std::vector<CatalogItem>& items,const CatalogRequestPolicy& policy)
{
    const std::string cache="ux0:/data/RBXV00001/system/catalog/pool-curation.json";
    std::string saved;readCatalogCache(cache,saved);
    if(saved.empty())readCatalogCache("app0:/catalog/pool-curation.json",saved);
    if(policy.mayFetch()){
        std::string fresh,error;
        if(fetchExternal(std::string(CatalogDownloadPolicy::poolOrigin())+"/place-pool/vita/curation.json",fresh,error)&&
           !policy.stopped()&&OwnedPlaceCatalog::applyCuration(fresh,items)){
            writeAtomic(cache,fresh);return;
        }
    }
    if(!policy.stopped())OwnedPlaceCatalog::applyCuration(saved,items);
}

inline bool serverCataloguesEnabled()
{
    static const bool enabled = fileExists("ux0:/data/RBXV00001/system/server-catalogues.enabled");
    return enabled;
}

inline std::vector<CatalogItem> onlineCatalog(std::string& status, bool forceRefresh = false,
                                            const CatalogRequestPolicy& policy = CatalogRequestPolicy())
{
    const std::string root="ux0:/data/RBXV00001/system/catalog";
    const std::string cache=root+"/pool-manifest.json";
    const std::string origin=CatalogDownloadPolicy::poolOrigin();
    sceIoMkdir(root.c_str(),0777);sceIoMkdir((root+"/pool-pages").c_str(),0777);
    std::string cached;readCatalogCache(cache,cached,128u*1024u);
    if(cached.empty()&&!serverCataloguesEnabled())
        readCatalogCache("app0:/catalog/pool-manifest.json",cached,128u*1024u);
    std::string lastError;
    const auto load=[&](const std::string& manifest,bool network,std::vector<CatalogItem>& result)->bool {
        std::vector<OwnedPlaceCatalog::Page> pages;size_t records=0;
        if(!OwnedPlaceCatalog::manifest(manifest,pages,records)){lastError="Invalid archive manifest.";return false;}
        std::vector<CatalogItem> staged;staged.reserve(records);
        for(const auto& page:pages){
            if(policy.stopped())return false;
            const std::string pageCache=root+"/pool-pages/"+page.file;
            std::string data;readCatalogCache(pageCache,data,page.bytes);
            bool valid=OwnedPlaceCatalog::validPage(page,data),downloaded=false;
            if(!valid&&!serverCataloguesEnabled()) {
                data.clear();readCatalogCache("app0:/catalog/pool-pages/"+page.file,data,page.bytes);
                valid=OwnedPlaceCatalog::validPage(page,data);
            }
            if(!valid&&network&&policy.mayFetch()) {
                data.clear();std::string error;
                if(!fetchExternal(origin+"/place-pool/vita/pages/"+page.file,data,error)) {
                    lastError=error;return false;
                }
                valid=OwnedPlaceCatalog::validPage(page,data);downloaded=valid;
            }
            if(!valid||!OwnedPlaceCatalog::parsePage(data,page.records,staged,supportedPlaceName)) {
                lastError="Archive page failed its integrity check.";return false;
            }
            // Content-addressed local/bundled pages are already verified. Only
            // a newly downloaded page needs publication, once per new content.
            if(downloaded&&!policy.stopped())writeAtomic(pageCache,data);
        }
        if(staged.size()!=records||policy.stopped())return false;
        result.swap(staged);return true;
    };
    std::vector<CatalogItem> result;
    if(!forceRefresh&&policy.cacheOnly&&!cached.empty()&&load(cached,false,result)){
        enrichOwnedCatalogue(result,policy);status="Bloxicon archive: "+std::to_string(result.size())+" places.";return result;
    }
    if(policy.mayFetch()&&!policy.stopped()){
        std::string fresh,error;
        if(fetchExternal(origin+"/place-pool/vita/manifest.json",fresh,error)&&load(fresh,true,result)){
            if(!policy.stopped())writeAtomic(cache,fresh);
            enrichOwnedCatalogue(result,policy);status="Bloxicon archive: "+std::to_string(result.size())+" places.";return result;
        }
        if(!error.empty())lastError=error;
    }
    if(!cached.empty()&&load(cached,false,result)){
        enrichOwnedCatalogue(result,policy);status="Bloxicon archive: "+std::to_string(result.size())+" saved places. Refresh unavailable.";return result;
    }
    status=policy.stopped()?"Archive loading cancelled.":"Bloxicon archive unavailable. "+lastError;
    return {};
}

inline std::vector<CatalogItem> filterCatalogItems(const std::vector<CatalogItem>& all,
                                                   const std::string& rawQuery)
{
    std::string query = lowerCopy(rawQuery);
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front())))
        query.erase(query.begin());
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back())))
        query.pop_back();
    if (query.empty())
        return all;

    std::vector<std::string> terms;
    std::istringstream words(query);
    std::string term;
    while (words >> term)
        terms.push_back(term);

    std::vector<CatalogItem> result;
    for (const CatalogItem& item : all)
    {
        const std::string haystack = lowerCopy(item.name + " " + item.source + " " +
            item.creator + " " + item.year);
        bool match = true;
        for (const std::string& word : terms)
            if (haystack.find(word) == std::string::npos)
            {
                match = false;
                break;
            }
        if (match)
            result.push_back(item);
    }
    return result;
}

inline bool installCatalogItem(const CatalogItem& item, std::string& status)
{
    if (!supportedPlaceName(item.name))
    {
        status = "Unsupported place format.";
        return false;
    }
    if (item.size > 48ull * 1024ull * 1024ull)
    {
        status = "This place is too large for the Vita downloader.";
        return false;
    }

    std::string name = safeFileName(item.name);
    if (!supportedPlaceName(name))
    {
        status = "Downloaded file has no supported Bloxicon extension.";
        return false;
    }

    sceIoMkdir("ux0:/data/RBXV00001", 0777);
    sceIoMkdir("ux0:/data/RBXV00001/places", 0777);
    const std::string destination=catalogInstallPath(item);
    if(fileExists(destination)){
        const std::atomic<bool> notCancelled{false};
        if(ArchiveIdentity::validHash(item.contentHash)&&ArchiveIdentity::fileHash(destination,fileSize(destination),notCancelled)==item.contentHash){
            status="Already installed.";return true;
        }
        status="A different original already uses this install path. It was preserved.";return false;
    }
    const std::string folder=destination.substr(0,destination.find_last_of('/'));
    sceIoMkdir(folder.c_str(),0777);
    const std::string tempPath = destination + ".download";
    const size_t maxBytes = 48u * 1024u * 1024u;
    size_t downloadedBytes = 0;
    std::string error;
    if (!downloadHasWorkingSpace(item, tempPath, error))
    {
        status = "Download paused: " + error;
        return false;
    }
    if (!fetchExternalToFile(item, tempPath, maxBytes, downloadedBytes, error))
    {
        status = "Download failed: " + error;
        return false;
    }

    std::string validationError;
    if (!looksLikeRobloxPlaceFile(tempPath, validationError))
    {
        clearDownloadResumeState(tempPath);
        status = "Download rejected: " + validationError;
        return false;
    }
    if(!item.contentHash.empty()){
        const std::atomic<bool> notCancelled{false};
        const auto digest=ArchiveIdentity::fileHash(tempPath,downloadedBytes,notCancelled);
        if(!ArchiveIdentity::validHash(item.contentHash)||digest!=item.contentHash){
            clearDownloadResumeState(tempPath);status="Download rejected: file does not match its archive record.";return false;
        }
    }
    if (!commitStagedFile(destination, tempPath))
    {
        status = "Could not commit downloaded place; previous install was preserved.";
        return false;
    }
    clearDownloadResumeState(tempPath);
    // The bytes may now identify a different published place. Never show stale
    // runtime identity, canonical metadata or an icon from the previous install.
    clearPlaceDetailCaches(destination);
    const bool metadataSaved = writeInstallMetadata(destination, item, downloadedBytes);

    std::ostringstream text;
    text << "Installed " << name << " (" << (downloadedBytes / 1024) << " KiB)";
    if (!item.source.empty())
        text << " from " << item.source;
    text << ".";
    if (!metadataSaved)
        text << " Origin metadata could not be saved; update detection may be unavailable.";
    status = text.str();
    return true;
}

inline bool installDirectUrl(const std::string& url, std::string& status)
{
    if(!CatalogDownloadPolicy::ownedObject(url)){
        status="Choose an original from the Bloxicon archive.";return false;
    }
    const auto items=onlineCatalog(status);
    for(const auto& item:items)if(item.url==url)return installCatalogItem(item,status);
    status="This original is not in the dated Bloxicon catalogue.";return false;
}

} // namespace VitaBrowser
