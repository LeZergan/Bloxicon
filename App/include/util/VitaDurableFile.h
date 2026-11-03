#pragma once
#if defined(RBX_HEADLESS_SERVER) && defined(RBX_PLATFORM_LINUX)
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <sys/stat.h>

namespace RBX { namespace VitaDurableFile {
inline FILE* stage(const std::string& path) {
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
    if(fd<0)return nullptr;
    struct stat info;
    if(::fstat(fd,&info)||!S_ISREG(info.st_mode)||(info.st_mode&0077)||::ftruncate(fd,0)){::close(fd);return nullptr;}
    FILE* file=::fdopen(fd,"wb");if(!file)::close(fd);return file;
}
// A successful save includes its directory entry, not only buffered file data.
inline void syncDirectory(const std::string& directory) {
    const int fd=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)throw std::runtime_error("Could not open save directory for durable commit");
    const int result=::fsync(fd);::close(fd);
    if(result)throw std::runtime_error("Could not durably commit save directory");
}
inline void syncParent(const std::string& path) {
    const size_t slash=path.find_last_of('/');
    syncDirectory(slash==std::string::npos?".":slash==0?"/":path.substr(0,slash));
}
} }
#endif
