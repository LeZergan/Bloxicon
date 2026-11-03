#pragma once
#include "util/VitaLocalDataStore.h"
#include "util/VitaLegacySave.h"
#include <dirent.h>
#include <ctime>
#include <map>

namespace VitaBrowser { namespace PlaceData {
struct File {std::string relative;unsigned long long bytes=0;long long modified=0;};
struct Record {File file;std::string type,scope,store,key,value,error;};
struct Snapshot {std::vector<Record> records;unsigned long long bytes=0;size_t files=0;long long modified=0;std::string error;};
inline bool safeRelative(const std::string& name) {
    if(name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos)return false;
    std::istringstream parts(name);std::string part;while(std::getline(parts,part,'/'))if(part.empty()||part=="."||part=="..")return false;return true;
}
inline void walk(const std::string& root,const std::string& relative,std::vector<File>& result,unsigned depth=0) {
    if(depth>6)throw std::runtime_error("Saved data has too many directory levels");
    const std::string folder=relative.empty()?root:root+"/"+relative;
    // Vita's directory wrapper does not consistently publish errno for a
    // missing folder. The file-stat path does; distinguish absence before
    // opening the directory without treating other failures as empty data.
    struct stat directoryInfo={};errno=0;
    if(::stat(folder.c_str(),&directoryInfo)!=0){if(errno==ENOENT)return;throw std::runtime_error("Could not inspect saved data information");}
    if((directoryInfo.st_mode&S_IFMT)!=S_IFDIR)throw std::runtime_error("Saved data location is not a directory");
    DIR* dir=opendir(folder.c_str());if(!dir){if(errno==ENOENT)return;throw std::runtime_error("Could not inspect saved data");}
    std::unique_ptr<DIR,int(*)(DIR*)> owner(dir,closedir);
    for(;;) {
        errno=0;dirent* entry=readdir(dir);
        if(!entry){if(errno)throw std::runtime_error("Could not read saved data directory");break;}
        const std::string name=entry->d_name;if(name=="."||name=="..")continue;
        if(!safeRelative(name)||name.find('/')!=std::string::npos)throw std::runtime_error("Invalid name in saved data");
        const std::string child=relative.empty()?name:relative+"/"+name;struct stat info={};
        if(::stat((root+"/"+child).c_str(),&info)!=0)throw std::runtime_error("Could not read saved data information");
        if((info.st_mode&S_IFMT)==S_IFDIR)walk(root,child,result,depth+1);
        else if((info.st_mode&S_IFMT)==S_IFREG) {
            if(result.size()>=4096||info.st_size<0||info.st_size>2*1024*1024)throw std::runtime_error("Saved data inspection limit exceeded");
            result.push_back({child,static_cast<unsigned long long>(info.st_size),static_cast<long long>(info.st_mtime)});
        } else throw std::runtime_error("Unsupported entry in saved data");
    }
}
inline std::string raw(const std::string& path,size_t limit=1024*1024) {
    struct stat status={};if(::stat(path.c_str(),&status)!=0||status.st_size<0||status.st_size>limit)throw std::runtime_error("Saved file is unavailable or too large");
    std::ifstream stream(path,std::ios::binary);std::string text(size_t(status.st_size),'\0');if(!text.empty())stream.read(&text[0],text.size());
    if(!stream||stream.peek()!=EOF)throw std::runtime_error("Could not read saved file");return text;
}
inline Snapshot inspect(const std::string& root) {
    Snapshot result;std::vector<File> files;
    try {
        walk(root,"",files);std::sort(files.begin(),files.end(),[](const File& a,const File& b){return a.relative<b.relative;});
        for(const auto& file:files) {
            result.bytes+=file.bytes;++result.files;result.modified=std::max(result.modified,file.modified);
            std::vector<std::string> parts;std::istringstream path(file.relative);std::string part;
            while(std::getline(path,part,'/'))parts.push_back(part);
            if(file.relative.size()>4&&file.relative.substr(file.relative.size()-4)==".tmp")continue;
            if(parts.size()==4&&(parts[0]=="standard"||parts[0]=="sorted")) {
                const std::string filename=parts[3];if(filename.size()>4&&filename.substr(filename.size()-4)==".bak")continue;
                if(filename.size()<6 || filename.substr(filename.size()-5)!=".save")continue;
                Record record;record.file=file;record.type=parts[0];
                try {record.scope=RBX::VitaLocalDataStore::storedName(parts[1]);record.store=RBX::VitaLocalDataStore::storedName(parts[2]);
                    record.key=RBX::VitaLocalDataStore::storedName(filename.substr(0,filename.size()-5));record.value=RBX::VitaLocalDataStore::inspectRecord(root+"/"+file.relative);
                }catch(const std::exception& e){record.error=e.what();record.key=filename;}
                result.records.push_back(std::move(record));
            } else if(parts.size()==2&&parts[0]=="legacy"&&parts[1].find(".xmlsave")!=std::string::npos&&parts[1].find(".previous")==std::string::npos) {
                Record record;record.file=file;record.type="Player API";record.key=parts[1];record.store="Classic player data";
                try{record.value=RBX::VitaLegacySave::read(root+"/"+file.relative);}catch(const std::exception& e){record.error=e.what();}
                result.records.push_back(std::move(record));
            }
        }
    }catch(const std::exception& e){result.error=e.what();}
    return result;
}
// A backup is a separate copy; it never moves or clears the committed saves.
inline void backup(const std::string& source,const std::string& target) {
    struct stat exists={};if(::stat(target.c_str(),&exists)==0)throw std::runtime_error("A backup already exists at this location");
    std::vector<File> files;walk(source,"",files);unsigned long long total=0;
    for(const auto& file:files){total+=file.bytes;if(total>32*1024*1024)throw std::runtime_error("Backup exceeds 32 MiB; existing data kept");}
    RBX::VitaLegacySave::directories(target);
    for(const auto& file:files) {
        if(!safeRelative(file.relative))throw std::runtime_error("Invalid backup path");
        const std::string destination=target+"/"+file.relative;const auto data=raw(source+"/"+file.relative,2*1024*1024);
        RBX::VitaLegacySave::directories(destination.substr(0,destination.find_last_of('/')));
        const std::string staged=destination+".tmp";std::ofstream out(staged,std::ios::binary);out.write(data.data(),data.size());out.close();
        if(!out||!RBX::VitaAtomicFile::commit(destination,staged)||raw(destination,2*1024*1024)!=data)
            throw std::runtime_error("Backup verification failed; original saves kept");
    }
    std::ofstream complete(target+"/backup.complete");complete<<"RBXV_BACKUP1\n"<<files.size()<<'\n';complete.close();
    if(!complete)throw std::runtime_error("Could not finalize backup");
}
} }
