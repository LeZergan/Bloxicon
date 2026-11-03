#pragma once
#include "VitaFileSystem.h"
#include "VitaDurableFile.h"
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
#include <sys/file.h>
#endif
// Local persistence transport for the original DataStore API. This is NOT
// a Roblox cloud service, purchase service, or account-data mirror. The host
// supplies a private, stable namespace for each installed local place.
// Kept independent of DataModel so the exact storage code is host-testable.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <filesystem>
#else
#include <dirent.h>
#endif
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace RBX {
class VitaLocalDataStore
{
    typedef std::map<std::string, std::string> Parameters;
    static const size_t maxValueBytes = 64 * 1024;
    static const size_t maxSortedRecords = 4096;
    std::string root;
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
    int rootLock = -1;
#endif

    static void fail(const char* message) { throw std::runtime_error(message); }
    static int nibble(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    static std::string decode(const std::string& text)
    {
        std::string result;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '%')
            {
                if (i + 2 >= text.size() || nibble(text[i+1]) < 0 || nibble(text[i+2]) < 0)
                    fail("Local DataStore: malformed encoded parameter");
                result += static_cast<char>((nibble(text[i+1]) << 4) | nibble(text[i+2]));
                i += 2;
            }
            else result += text[i] == '+' ? ' ' : text[i];
        }
        return result;
    }
    static Parameters parameters(const std::string& text)
    {
        if (text.size() > 6 * maxValueBytes + 32768) fail("Local DataStore: request too large");
        Parameters result;
        size_t begin = 0;
        while (begin < text.size())
        {
            size_t end = text.find('&', begin);
            if (end == std::string::npos) end = text.size();
            if (end != begin)
            {
                size_t equal = text.find('=', begin);
                if (equal == std::string::npos || equal >= end) fail("Local DataStore: malformed parameter");
                std::string key = decode(text.substr(begin, equal-begin));
                if (!result.insert(std::make_pair(key, decode(text.substr(equal+1, end-equal-1)))).second)
                    fail("Local DataStore: duplicate parameter");
                if (result.size() > 400) fail("Local DataStore: too many parameters");
            }
            begin = end + 1;
        }
        return result;
    }
    static const std::string& required(const Parameters& values, const std::string& key)
    {
        Parameters::const_iterator found = values.find(key);
        if (found == values.end()) fail("Local DataStore: missing parameter");
        return found->second;
    }
    static std::string encodeComponent(const std::string& value)
    {
        if (value.size() > 50) fail("Local DataStore: name, scope or key too long");
        static const char hex[] = "0123456789abcdef";
        std::string result("h");
        for (size_t i = 0; i < value.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(value[i]);
            result += hex[c >> 4]; result += hex[c & 15];
        }
        return result;
    }
    static std::string decodeComponent(const std::string& value)
    {
        if (value.empty() || value[0] != 'h' || !(value.size() & 1) || value.size() > 101)
            fail("Local DataStore: invalid stored key filename");
        std::string result;
        for (size_t i = 1; i < value.size(); i += 2)
        {
            if (nibble(value[i]) < 0 || nibble(value[i+1]) < 0) fail("Local DataStore: invalid stored key filename");
            result += static_cast<char>((nibble(value[i]) << 4) | nibble(value[i+1]));
        }
        return result;
    }
    static bool exists(const std::string& path)
    {
        VitaFileSystem::Status info;
        if (VitaFileSystem::status(path, &info) == 0) return true;
        if (errno != ENOENT) fail("Local DataStore: cannot inspect save storage");
        return false;
    }
    static void directory(const std::string& path)
    {
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
        int result = ::mkdir(path.c_str(),0700);
#else
        int result = VitaFileSystem::mkdir(path);
#endif
        if (result != 0 && errno != EEXIST) fail("Local DataStore: cannot create save directory");
        VitaFileSystem::Status info;
        if (VitaFileSystem::status(path, &info) != 0 || (info.st_mode & S_IFMT) != S_IFDIR)
            fail("Local DataStore: save path is not a directory");
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
        if(result==0)VitaDurableFile::syncParent(path);
#endif
    }
    static void directories(const std::string& path)
    {
        // Only the trusted host root is traversed. All script-controlled names
        // become hex components; ../, slashes, NULs and case cannot escape it.
        for (size_t i = 1; i <= path.size(); ++i)
            if (i == path.size() || path[i] == '/')
            {
                std::string part = path.substr(0, i);
                if (part.empty() || part[part.size()-1] == ':') continue;
                directory(part);
            }
    }
    std::string storePath(const std::string& type, const std::string& scope, const std::string& name) const
    {
        if (type != "standard" && type != "sorted") fail("Local DataStore: unsupported store type");
        if (scope.empty()) fail("Local DataStore: empty scope");
        return root + "/" + type + "/" + encodeComponent(scope) + "/" + encodeComponent(name);
    }
    static std::string quote(const std::string& value)
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        // The vendored writer also requires an object/array root.
        writer.StartArray();
        writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        writer.EndArray();
        return std::string(buffer.GetString() + 1, buffer.Size() - 2);
    }
    static void parseValue(const std::string& text, rapidjson::Document& document)
    {
        if (text.empty() || text.size() > maxValueBytes || text.find('\0') != std::string::npos)
            fail("Local DataStore: invalid or oversized value");
        // This repository's original RapidJSON predates scalar roots and
        // iterative parsing. Bound recursion and wrap exactly one JSON value.
        unsigned depth = 0;
        bool quoted = false, escaped = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
            }
            else if (c == '"') quoted = true;
            else if (c == '{' || c == '[')
            {
                if (++depth > 64) fail("Local DataStore: JSON nesting exceeds local limit");
            }
            else if (c == '}' || c == ']')
            {
                if (!depth) fail("Local DataStore: malformed JSON nesting");
                --depth;
            }
        }
        if (quoted || depth) fail("Local DataStore: incomplete JSON value");
        std::string wrapped = "[" + text + "]";
        document.Parse<0>(wrapped.c_str());
        if (document.HasParseError() || !document.IsArray() || document.Size() != 1)
            fail("Local DataStore: malformed JSON value");
    }
    static void validateValue(const std::string& value, bool ordered)
    {
        if (value.empty() || value.size() > maxValueBytes || value.find('\0') != std::string::npos)
            fail("Local DataStore: invalid or oversized value");
        rapidjson::Document document;
        parseValue(value, document);
        const rapidjson::Value& parsed = document[0u];
        if (parsed.IsNull()) fail("Local DataStore: value must be non-null JSON");
        if (ordered && (!parsed.IsNumber() || !std::isfinite(parsed.GetDouble()) ||
                        std::fabs(parsed.GetDouble()) > 9007199254740991.0 ||
                        std::floor(parsed.GetDouble()) != parsed.GetDouble()))
            fail("Local DataStore: ordered values must be finite integers");
    }
    static uint32_t checksum(const std::string& value)
    {
        // Corruption/incomplete-write detection only, not authentication.
        uint32_t crc = 0xffffffffu;
        for (size_t i = 0; i < value.size(); ++i)
        {
            crc ^= static_cast<unsigned char>(value[i]);
            for (int bit = 0; bit != 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }
    static std::string readRecord(const std::string& path)
    {
        VitaFileSystem::Status info;
        if (VitaFileSystem::status(path, &info) != 0 || (info.st_mode & S_IFMT) != S_IFREG ||
            info.st_size < 1 || info.st_size > static_cast<long long>(maxValueBytes + 80))
            fail("Local DataStore: invalid save file; existing data was not reset");
        std::unique_ptr<FILE, int(*)(FILE*)> file(VitaFileSystem::open(path, "rb"), &std::fclose);
        if (!file) fail("Local DataStore: cannot read save file");
        std::string data(static_cast<size_t>(info.st_size), '\0');
        if (std::fread(&data[0], 1, data.size(), file.get()) != data.size() || std::fgetc(file.get()) != EOF || std::ferror(file.get()))
            fail("Local DataStore: incomplete save read");
        size_t newline = data.find('\n');
        if (newline == std::string::npos || newline > 70) fail("Local DataStore: damaged save header");
        std::istringstream header(data.substr(0, newline));
        std::string magic, extra;
        size_t length = 0; uint32_t crc = 0;
        if (!(header >> magic >> std::hex >> length >> crc) || magic != "RBXLOCAL1" || (header >> extra))
            fail("Local DataStore: damaged save header");
        std::string value = data.substr(newline + 1);
        if (value.size() != length || checksum(value) != crc)
            fail("Local DataStore: damaged save checksum; existing data was not reset");
        validateValue(value, false);
        return value;
    }
    static std::string readValue(const std::string& path)
    {
        if (exists(path)) return readRecord(path); // Never hide corruption as a new profile.
        if (exists(path + ".bak")) return readRecord(path + ".bak"); // Interrupted rename.
        return std::string(); // A genuinely new key, represented as nil by original DataStore.
    }
    static void eraseIfExists(const std::string& path)
    {
        if (VitaFileSystem::remove(path) != 0 && errno != ENOENT)
            fail("Local DataStore: cannot rotate save backup");
    }
    static void writeValue(const std::string& folder, const std::string& path, const std::string& value)
    {
        directories(folder);
        // Validate any existing committed data before replacing it or its backup.
        const bool currentExists = exists(path);
        if (currentExists) readRecord(path);
        else if (exists(path + ".bak")) readRecord(path + ".bak");
        const std::string temporary = path + ".tmp";
        std::ostringstream header;
        header << "RBXLOCAL1 " << std::hex << value.size() << " " << checksum(value) << "\n";
        std::string data = header.str() + value;
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
        FILE* file = VitaDurableFile::stage(temporary);
#else
        FILE* file = VitaFileSystem::open(temporary, "wb");
#endif
        if (!file) fail("Local DataStore: cannot open temporary save; old data retained");
        bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
        if (std::fflush(file) != 0) ok = false;
#ifdef _WIN32
        if (::_commit(::_fileno(file)) != 0) ok = false;
#else
        if (::fsync(::fileno(file)) != 0) ok = false;
#endif
        if (std::fclose(file) != 0) ok = false;
        if (!ok) fail("Local DataStore: save write or flush failed; old data retained");
        if (readRecord(temporary) != value) fail("Local DataStore: temporary readback mismatch");
        if (currentExists)
        {
            eraseIfExists(path + ".bak");
            if (VitaFileSystem::rename(path, path + ".bak") != 0)
                fail("Local DataStore: cannot preserve previous save");
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
            VitaDurableFile::syncDirectory(folder);
#endif
        }
        if (VitaFileSystem::rename(temporary, path) != 0)
        {
            // Best-effort restoration; even if this fails, readValue recovers .bak.
            if (currentExists) VitaFileSystem::rename(path + ".bak", path);
            fail("Local DataStore: save commit failed; previous backup retained");
        }
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
        VitaDurableFile::syncDirectory(folder);
#endif
    }
    static double integer(const std::string& text)
    {
        rapidjson::Document document;
        parseValue(text, document);
        if (!document[0u].IsNumber())
            fail("Local DataStore: value is not an integer");
        double value = document[0u].GetDouble();
        if (!std::isfinite(value) || value != std::floor(value) || std::fabs(value) > 9007199254740991.0)
            fail("Local DataStore: integer is outside the exact Lua number range");
        return value;
    }
    static std::string number(double value)
    {
        if (!std::isfinite(value) || value != std::floor(value) || std::fabs(value) > 9007199254740991.0)
            fail("Local DataStore: integer overflow");
        std::ostringstream output;
        output << std::fixed << std::setprecision(0) << value;
        return output.str();
    }
    struct Entry { std::string key; double value; };
    static bool before(const Entry& a, const Entry& b, bool ascending)
    {
        return a.value == b.value ? a.key < b.key : (ascending ? a.value < b.value : a.value > b.value);
    }
    std::string sorted(const Parameters& query)
    {
        if (required(query, "type") != "sorted") fail("Local DataStore: sorted query on a normal store");
        const double pageNumber = integer(required(query, "pageSize"));
        if (pageNumber < 1 || pageNumber > 100) fail("Local DataStore: page size must be 1..100");
        const bool ascending = required(query, "ascending") == "True";
        if (!ascending && required(query, "ascending") != "False") fail("Local DataStore: invalid sort direction");
        const double low = query.count("inclusiveMinValue") ? integer(required(query,"inclusiveMinValue")) : -9007199254740991.0;
        const double high = query.count("inclusiveMaxValue") ? integer(required(query,"inclusiveMaxValue")) : 9007199254740991.0;
        if (low > high) fail("Local DataStore: minimum exceeds maximum");
        const std::string folder = storePath("sorted", required(query,"scope"), required(query,"key"));
        std::set<std::string> names;
        if (exists(folder))
        {
            auto collectName = [&](std::string filename) {
                if (filename.size() > 9 && filename.compare(filename.size()-9,9,".save.bak") == 0)
                    filename.resize(filename.size()-4);
                if (filename.size() < 6 || filename.compare(filename.size()-5,5,".save") != 0) return;
                names.insert(filename.substr(0,filename.size()-5));
                if (names.size() > maxSortedRecords) fail("Local DataStore: ordered store exceeds local scan limit");
            };
#ifdef _WIN32
            // MSVC has no POSIX dirent. Preserve the same bounded filename
            // scan and interrupted-commit deduplication on the PC host.
            std::error_code scanError;
            std::filesystem::directory_iterator cursor(std::filesystem::u8path(folder),scanError),end;
            if(scanError)fail("Local DataStore: cannot enumerate ordered keys");
            for(;cursor!=end;cursor.increment(scanError)) {
                if(scanError)fail("Local DataStore: ordered key enumeration failed");
                collectName(cursor->path().filename().string());
            }
            if(scanError)fail("Local DataStore: ordered key enumeration failed");
#else
            std::unique_ptr<DIR, int(*)(DIR*)> dir(::opendir(folder.c_str()), &::closedir);
            if (!dir) fail("Local DataStore: cannot enumerate ordered keys");
            errno = 0;
            while (struct dirent* entry = ::readdir(dir.get()))
            {
                collectName(entry->d_name);
            }
            if (errno != 0) fail("Local DataStore: ordered key enumeration failed");
#endif
        }
        std::vector<Entry> entries;
        for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
        {
            Entry entry = {decodeComponent(*it), integer(readValue(folder + "/" + *it + ".save"))};
            if (entry.value >= low && entry.value <= high) entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [ascending](const Entry& a, const Entry& b) { return before(a,b,ascending); });
        size_t start = 0;
        if (query.count("exclusiveStartKey"))
        {
            const std::string& cursor = required(query,"exclusiveStartKey");
            const size_t separator = cursor.find(':');
            if (separator == std::string::npos) fail("Local DataStore: invalid page cursor");
            Entry last = {decodeComponent(cursor.substr(separator+1)), integer(cursor.substr(0,separator))};
            while (start < entries.size() && !before(last, entries[start], ascending)) ++start;
        }
        size_t end = std::min(entries.size(), start + static_cast<size_t>(pageNumber));
        std::string response = "{\"data\":{\"Entries\":[";
        for (size_t i = start; i < end; ++i)
        {
            if (i != start) response += ',';
            response += "{\"Target\":" + quote(entries[i].key) + ",\"Value\":" + number(entries[i].value) + "}";
        }
        response += ']';
        if (end < entries.size() && end > start)
            response += ",\"ExclusiveStartKey\":" + quote(number(entries[end-1].value) + ":" + encodeComponent(entries[end-1].key));
        return response + "}}";
    }
public:
    static std::string inspectRecord(const std::string& path) { return readRecord(path); }
    static std::string storedName(const std::string& component) { return decodeComponent(component); }
    explicit VitaLocalDataStore(const std::string& hostRoot) : root(hostRoot)
    {
        if (root.empty() || root.find('\0') != std::string::npos) fail("Local DataStore: missing host namespace");
        std::replace(root.begin(), root.end(), '\\', '/');
        while (root.size() > 1 && root[root.size()-1] == '/') root.resize(root.size()-1);
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
        directories(root);
        const int fd=::open((root+"/.writer.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
        if(fd<0)fail("Local DataStore: cannot open private writer lock");
        struct stat info={};
        if(::fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) || (info.st_mode&0077) || ::flock(fd,LOCK_EX|LOCK_NB)!=0) {
            ::close(fd);
            fail("Local DataStore: save namespace already in use or writer lock is unsafe");
        }
        rootLock=fd;
#endif
    }
    ~VitaLocalDataStore() {
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
        if(rootLock>=0)::close(rootLock);
#endif
    }
    VitaLocalDataStore(const VitaLocalDataStore&)=delete;
    VitaLocalDataStore& operator=(const VitaLocalDataStore&)=delete;
    // Calls must be serialized by the owning worker. No Lua state or DataModel
    // is touched here, and no network fallback exists on any error path.
    std::string transact(const std::string& url, const std::string& postData)
    {
        const size_t question = url.find('?');
        if (question == std::string::npos) fail("Local DataStore: missing request query");
        const size_t slash = url.rfind('/', question);
        const std::string operation = url.substr(slash == std::string::npos ? 0 : slash+1, question-(slash == std::string::npos ? 0 : slash+1));
        const Parameters query = parameters(url.substr(question+1));
        const Parameters body = parameters(postData);
        const std::string& type = required(query,"type");
        if (operation == "getSortedValues") return sorted(query);
        if (operation == "getV2")
        {
            std::string response = "{\"data\":[";
            bool first = true;
            size_t seen = 0;
            for (size_t i = 0; i < 100; ++i)
            {
                const std::string prefix = "qkeys[" + std::to_string(i) + "].";
                if (!body.count(prefix+"key")) break;
                const std::string& scope = required(body,prefix+"scope");
                if (scope != required(query,"scope")) fail("Local DataStore: batch scope mismatch");
                const std::string& key = required(body,prefix+"key");
                const std::string& target = required(body,prefix+"target");
                const std::string path = storePath(type,scope,key) + "/" + encodeComponent(target) + ".save";
                const std::string value = readValue(path);
                if (!value.empty())
                {
                    if (!first) response += ',';
                    first = false;
                    response += "{\"Value\":" + quote(value) + ",\"Scope\":" + quote(scope) + ",\"Key\":" + quote(key) + ",\"Target\":" + quote(target) + "}";
                }
                ++seen;
            }
            if (!seen || body.size() != seen * 3) fail("Local DataStore: malformed batch keys");
            return response + "]}";
        }
        if (operation != "set" && operation != "increment") fail("Local DataStore: unsupported operation");
        const std::string folder = storePath(type,required(query,"scope"),required(query,"key"));
        const std::string path = folder + "/" + encodeComponent(required(query,"target")) + ".save";
        const std::string current = readValue(path);
        std::string value;
        if (operation == "set")
        {
            value = required(body,"value");
            validateValue(value, type == "sorted");
            if (integer(required(query,"valueLength")) != static_cast<double>(value.size()))
                fail("Local DataStore: value length mismatch");
            if (query.count("expectedValueLength"))
            {
                const std::string& expected = required(body,"expectedValue");
                if (integer(required(query,"expectedValueLength")) != static_cast<double>(expected.size()))
                    fail("Local DataStore: expected value length mismatch");
                if (current != expected)
                    return "{\"error\":\"Local compare-and-set conflict\",\"currentValue\":" + (current.empty() ? "null" : quote(current)) + "}";
            }
        }
        else value = number((current.empty() ? 0 : integer(current)) + integer(required(query,"value")));
        writeValue(folder,path,value);
        return "{\"data\":" + quote(value) + "}";
    }
};
} // namespace RBX
