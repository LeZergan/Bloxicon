#pragma once
#include "VitaFileSystem.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace RBX { namespace VitaEmbeddedAssetBundle {
enum Result { Installed, AlreadyPresent, Conflict, Invalid, IoFailure };
inline bool parseName(const std::string& name, std::string& id, std::string& digest) {
    const size_t colon=name.find(':');
    if (colon==std::string::npos || colon==0 || colon>20 || name.size()!=colon+65) return false;
    id=name.substr(0,colon); digest=name.substr(colon+1);
    return id[0]!='0' && id.find_first_not_of("0123456789")==std::string::npos &&
        digest.find_first_not_of("0123456789abcdef")==std::string::npos;
}
template<class Hash> std::string fileDigest(const std::string& path) {
    FILE* file=VitaFileSystem::open(path,"rb");if(!file)return std::string();
    // Vita storage has a high per-read cost. Hash sequentially in large blocks,
    // without placing a 64 KiB buffer on the loader's bounded thread stack.
    char fallback[4096];char* allocated=static_cast<char*>(std::malloc(65536));
    char* buffer=allocated?allocated:fallback;const size_t capacity=allocated?65536:sizeof(fallback);
    Hash hash;size_t count;bool failed=false;
    while((count=std::fread(buffer,1,capacity,file))!=0)
        if(!hash.update(buffer,count)){failed=true;break;}
    failed=failed || std::ferror(file)!=0;std::fclose(file);std::free(allocated);
    return failed?std::string():hash.finish();
}
template<class Hash> Result install(const std::string& root,const std::string& name,
                                   const std::string& bytes) {
    std::string id,digest;
    if(!parseName(name,id,digest) || bytes.empty() || bytes.size()>16u*1024u*1024u)return Invalid;
    Hash hash;if(!hash.update(bytes.data(),bytes.size()) || hash.finish()!=digest)return Invalid;
    const std::string path=root+"/"+id;
    VitaFileSystem::Status info;
    if(VitaFileSystem::status(path,&info)==0)
        return fileDigest<Hash>(path)==digest?AlreadyPresent:Conflict;
    const std::string temporary=path+".embedded-staged";
    // A previous interrupted staging file is preserved for diagnosis.
    if(VitaFileSystem::status(temporary,&info)==0)return IoFailure;
    FILE* output=VitaFileSystem::open(temporary,"wb");if(!output)return IoFailure;
    const bool written=std::fwrite(bytes.data(),1,bytes.size(),output)==bytes.size();
    const bool closed=std::fclose(output)==0;
    if(!written || !closed || fileDigest<Hash>(temporary)!=digest){VitaFileSystem::remove(temporary);return IoFailure;}
    if(VitaFileSystem::status(path,&info)==0){VitaFileSystem::remove(temporary);return Conflict;}
    if(VitaFileSystem::rename(temporary, path)!=0){VitaFileSystem::remove(temporary);return IoFailure;}
    // Rename publishes the already read-back, verified file without changing
    // its contents. Avoid reading every newly installed payload a second time.
    return Installed;
}
} }
