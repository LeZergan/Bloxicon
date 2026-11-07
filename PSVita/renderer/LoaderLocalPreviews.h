#pragma once

#include "LoaderDetailsNetwork.h"
#include "LoaderPlaceData.h"
#include "LocalPlacePreviewRaster.h"
#include "v8datamodel/PartInstance.h"
#include "v8world/Primitive.h"
#include "v8xml/SerializerV2.h"
#include "G3D/System.h"
#include <cctype>
#include <cstring>
#include <map>
#include <memory>

namespace VitaBrowser { namespace LocalPreviews {
inline const char* directory(){return "ux0:/data/Bloxicon/system/local-previews";}
inline std::string preference(){return std::string(directory())+"/enabled";}
inline bool generated(const std::string& path){return path.compare(0,std::strlen(directory()),directory())==0;}
inline void makeDirectory(){sceIoMkdir(directory(),0777);}
inline bool isPlace(const std::string& path) {
    const auto dot=path.find_last_of('.');
    if(dot==std::string::npos)return false;
    auto suffix=path.substr(dot);
    for(auto& c:suffix)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return suffix==".rbxl"||suffix==".rbxlx";
}
inline uint32_t word(const std::string& bytes,size_t at) {
    uint32_t result=0;
    for(unsigned i=0;i<4;++i)result|=uint32_t(static_cast<unsigned char>(bytes[at+i]))<<(8*i);
    return result;
}
inline bool budgetAllows(const std::string& bytes) {
    if(bytes.compare(0,8,"<roblox!")!=0)return bytes.size()<=512*1024;
    if(bytes.size()<32||word(bytes,16)>512||word(bytes,20)>12000)return false;
    size_t expansion=0;
    for(size_t i=32;i+16<=bytes.size();) {
        const size_t compressed=word(bytes,i+4),plain=word(bytes,i+8),stored=compressed?compressed:plain;
        if(stored>bytes.size()-i-16||plain>4*1024*1024||plain>16*1024*1024-expansion)return false;
        expansion+=plain;
        if(bytes.compare(i,4,"END\0",4)==0)return true;
        i+=16+stored;
    }
    return false;
}
inline std::string readOriginal(const std::string& path) {
    constexpr size_t maxBytes=2*1024*1024;
    const auto size=fileSize(path);
    if(size<8||size>maxBytes)return {};
    std::ifstream in(path.c_str(),std::ios::binary);
    std::string bytes(size,'\0');
    if(!in.read(&bytes[0],bytes.size())||in.peek()!=std::char_traits<char>::eof())return {};
    return bytes;
}
struct Result {std::string image;const char* status="Preview unavailable";};
inline Result build(const std::string& path,const std::atomic<bool>& cancel) {
    const auto bytes=readOriginal(path);
    if(cancel.load())return {};
    if(bytes.empty())return {{}, "Place exceeds preview limit"};
    unsigned char digest[SHA256_DIGEST_LENGTH];
    if(!SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest))return {};
    static const char hex[]="0123456789abcdef";
    std::string hash;hash.reserve(sizeof(digest)*2);
    for(auto byte:digest){hash+=hex[byte>>4];hash+=hex[byte&15];}
    const auto target=std::string(directory())+"/v1-"+hash+".png";
    if(validCachedPng(target))return {target,"Preview ready"};
    if(!budgetAllows(bytes))return {{}, "Place exceeds preview limit"};
    using namespace LocalPreviewRaster;
    std::vector<Part> parts;
    {
        std::istringstream stream(bytes,std::ios::in|std::ios::binary);
        RBX::Instances roots;
        SerializerV2 serializer;
        serializer.loadInstances(stream,roots);
        const auto add=[&](const boost::shared_ptr<RBX::Instance>& instance){
            if(cancel.load(std::memory_order_relaxed)||parts.size()>=12000)return;
            auto* part=RBX::Instance::fastDynamicCast<RBX::PartInstance>(instance.get());
            if(!part||part->getTransparencyXml()>=.98f)return;
            Shape shape;
            const auto* primitive=RBX::PartInstance::getPrimitive(part);
            if(!primitive)return;
            switch(primitive->getGeometryType()){
                case RBX::Geometry::GEOMETRY_BLOCK:shape=Block;break;
                case RBX::Geometry::GEOMETRY_BALL:shape=Ball;break;
                case RBX::Geometry::GEOMETRY_CYLINDER:shape=Cylinder;break;
                case RBX::Geometry::GEOMETRY_WEDGE:shape=Wedge;break;
                case RBX::Geometry::GEOMETRY_CORNERWEDGE:shape=CornerWedge;break;
                default:return;
            }
            const auto v=[](const G3D::Vector3& p){return V{p.x,p.y,p.z};};
            const auto& cf=part->getCoordinateFrame();const auto color=part->getColor3();
            const Part sample{v(cf.translation),v(part->getPartSizeUi()),
                v(cf.rotation*G3D::Vector3(1,0,0)),v(cf.rotation*G3D::Vector3(0,1,0)),
                v(cf.rotation*G3D::Vector3(0,0,1)),{color.r,color.g,color.b},shape};
            if(finite(sample.center)&&finite(sample.size)&&finite(sample.axisX)&&finite(sample.axisY)&&
                finite(sample.axisZ)&&finite(sample.color)&&std::min({sample.size.x,sample.size.y,sample.size.z})>0)
                parts.push_back(sample);
        };
        const RBX::Instance* workspace=nullptr;
        for(const auto& root:roots)if(root->getClassNameStr()=="Workspace"||root->getName()=="Workspace"){workspace=root.get();break;}
        if(workspace)workspace->visitDescendants(add);
        else for(const auto& root:roots)if(root->getClassNameStr()=="Model"||RBX::Instance::fastDynamicCast<RBX::PartInstance>(root.get())) {
            add(root);root->visitDescendants(add);
        }
    }
    if(parts.empty())return {{}, "No supported map geometry"};
    auto pixels=render(parts,cancel);
    if(cancel.load())return {};
    if(pixels.rgb.empty()||!pixels.triangles)return {{},"Map too complex to preview"};
    G3D::GImage picture;
    picture.resize(pixels.width,pixels.height,3,false);
    std::memcpy(picture.byte(),pixels.rgb.data(),pixels.rgb.size());
    unsigned char* encoded=nullptr;int length=0;
    picture.encode(G3D::GImage::PNG,encoded,length);
    const std::unique_ptr<unsigned char,decltype(&G3D::System::free)> owned(encoded,&G3D::System::free);
    if(!encoded||length<=0||cancel.load()||readOriginal(path)!=bytes||cancel.load())return {};
    makeDirectory();
    if(!writeAtomic(target,std::string(reinterpret_cast<const char*>(encoded),length)))return {};
    return {target,"Preview ready"};
}

class Service {
    struct Cached {Result preview;unsigned long long stamp=0,size=0;uint64_t nextCheck=0;};
    LoaderAsyncTask<Result> work;
    std::map<std::string,Cached> results;
    std::string active;
    unsigned long long activeStamp=0,activeSize=0;
    bool on=fileExists(preference());
public:
    bool enabled()const{return on;}
    void toggle() {
        makeDirectory();
        if(on) {
            if(std::remove(preference().c_str())!=0)return;
            on=false;work.cancel();
        }else {
            if(!writeAtomic(preference(),"1\n"))return;
            on=true;
            for(auto it=results.begin();it!=results.end();)
                if(it->second.preview.image.empty())it=results.erase(it);else ++it;
        }
    }
    void pause() {
        work.cancel();
        while(!work.settled())sceKernelDelayThread(8000);
        active.clear();
    }
    Result request(const std::string& path) {
        const uint64_t now=sceKernelGetSystemTimeWide();
        if(!active.empty()){
            if(!work.settled()){
                const auto found=results.find(path);
                if(found!=results.end())return found->second.preview;
                return {{},!on?"Preview off":path==active?"Creating preview":"Preview queued"};
            }
            Result value;std::string error;
            const auto state=work.poll(value,error);
            if(state!=LoaderAsyncTask<Result>::Cancelled){
                if(results.size()>=256)results.erase(results.begin());
                results[active]={value,activeStamp,activeSize,now};
            }
            active.clear();
        }
        const auto found=results.find(path);
        if(found!=results.end()){
            auto& cached=found->second;
            if(now<cached.nextCheck)return cached.preview;
            if(metadataSourceStamp(path)==cached.stamp&&fileSize(path)==cached.size){
                cached.nextCheck=now+4000000;
                return cached.preview;
            }
            results.erase(found);
        }
        if(!on||!isPlace(path))return {{},on?"Preview unavailable":"Preview off"};
        const auto schedule=[](std::function<void()> fn){
            static RBX::ThreadPool worker(1,RBX::BaseThreadPool::WaitForRunningTasks,1);
            return worker.schedule([fn](boost::shared_ptr<rbx::spin_mutex>){fn();});
        };
        if(work.start(schedule,[path](const std::atomic<bool>& flag){return build(path,flag);})) {
            active=path;
            activeStamp=metadataSourceStamp(path);activeSize=fileSize(path);
            return {{},"Creating preview"};
        }
        return {};
    }
};
inline Service& service(){static Service instance;return instance;}
}}
