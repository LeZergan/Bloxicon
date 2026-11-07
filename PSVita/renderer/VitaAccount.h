#pragma once
#include "AvatarAccountReplica.h"
#include "AvatarFileRead.h"
#include "LoaderAsyncTask.h"
#include "util/ThreadPool.h"
#include <curl/curl.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <psp2/kernel/processmgr.h>
#include "network/VitaTcpTunnel.h"
#include "util/BloxiconPolicy.h"
#include "util/VitaCurlConnection.h"
#include "util/VitaCurlBootstrap.h"
#include "AccountSessionStore.h"
#include "VitaAccountTransportDiagnostic.h"
#include "VitaPackRange.h"
#include "util/BloxiconSocial.h"
#include <psp2/kernel/openpsid.h>
#include <cstring>

namespace VitaAccount {
using Doc=rapidjson::Document;
inline void require(bool valid,const char* message){if(!valid)throw std::runtime_error(message);}
inline bool read(const std::string& path,size_t maximum,std::string& bytes,bool optional=false,bool* missing=nullptr){
    VitaBrowser::recoverAtomicTarget(path);
    return VitaAvatar::readBounded(path,maximum,bytes,optional,missing);
}
inline bool write(const std::string& path,const std::string& bytes){
    static constexpr char avatarRoot[]="ux0:/data/Bloxicon/system/avatar/";
    if(path.compare(0,sizeof(avatarRoot)-1,avatarRoot)==0)
        sceIoMkdir("ux0:/data/Bloxicon/system/avatar",0777);
    VitaBrowser::recoverAtomicTarget(path);auto temporary=path+".account-pending";
    {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);if(!output)return false;output.write(bytes.data(),bytes.size());output.close();if(!output)return false;}
    std::string verified;
    if(!VitaAvatar::readBounded(temporary,65536,verified)||verified!=bytes){std::remove(temporary.c_str());return false;}
    if(!VitaBrowser::commitAtomicTemp(path,temporary)){std::remove(temporary.c_str());return false;}
    return VitaAvatar::readBounded(path,65536,verified)&&verified==bytes;
}
inline std::string field(const rapidjson::Value& value,const char* key){require(value.IsObject()&&value.HasMember(key)&&value[key].IsString(),"Invalid account response");return {value[key].GetString(),value[key].GetStringLength()};}
inline Doc parse(const std::string& bytes){require(!bytes.empty()&&bytes.find('\0')==std::string::npos,"Invalid account response");Doc result;result.Parse<0>(bytes.c_str());require(!result.HasParseError()&&result.IsObject(),"Invalid account response");return result;}
inline std::string json(const Doc& value){rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer> w(b);value.Accept(w);return {b.GetString(),b.Size()};}
inline void addString(Doc& value,const char* key,const std::string& text){rapidjson::Value member(text.c_str(),value.GetAllocator());value.AddMember(key,member,value.GetAllocator());}
inline std::string object(const char* key,const std::string& value){Doc d;d.SetObject();rapidjson::Value name(key,d.GetAllocator()),text(value.c_str(),d.GetAllocator());d.AddMember(name,text,d.GetAllocator());return json(d);}
inline std::string randomKey(){unsigned char bytes[24];require(RAND_bytes(bytes,sizeof(bytes))==1,"Secure randomness is unavailable");const char* h="0123456789abcdef";std::string out;for(auto b:bytes){out+=h[b>>4];out+=h[b&15];}return out;}
inline bool uuid(const std::string& value){if(value.size()!=36)return false;for(size_t i=0;i<value.size();++i)if(i==8||i==13||i==18||i==23){if(value[i]!='-')return false;}else if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;return true;}
inline bool token(const std::string& value){return value.size()==43&&std::all_of(value.begin(),value.end(),[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_'||c=='-';});}
inline std::string digest(const std::string& bytes){unsigned char out[32];SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),out);const char* h="0123456789abcdef";std::string result;for(auto b:out){result+=h[b>>4];result+=h[b&15];}return result;}
inline Doc configuration(){
    std::string bytes;const char* path="ux0:/data/Bloxicon/system/site.json";bool missing=false;
    require(read(path,4096,bytes,true,&missing),"Could not read the configured account service.");
    if(missing){
        path="ux0:/data/Bloxicon/content/bloxicon-defaults/site.json";
        require(read(path,4096,bytes,true,&missing),"Could not read the bundled account origin.");
        if(missing){path="app0:/site.json";require(read(path,4096,bytes)&&!bytes.empty(),"The host must configure site.json with its HTTPS account origin.");}
    }
    return parse(bytes);
}
inline std::string origin(){
    auto value=field(configuration(),"origin");require(value.compare(0,8,"https://")==0&&value.size()<=300,"The account service requires HTTPS.");
    require(RBX::Bloxicon::lower(value).find("roblox")==std::string::npos,"Configure the actual Bloxicon website address in site.json before signing in.");
    auto authority=value.substr(8);require(!authority.empty()&&authority.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:")==std::string::npos&&authority.find('.')!=std::string::npos,"Invalid account service origin");
    return value;
}
inline unsigned short gamePort(){auto d=configuration();if(!d.HasMember("game_port"))return 31901;require(d["game_port"].IsUint()&&d["game_port"].GetUint()>0&&d["game_port"].GetUint()<=65535,"Invalid online service port.");return static_cast<unsigned short>(d["game_port"].GetUint());}
struct ApiError:std::runtime_error{std::string code;long status;unsigned retry;ApiError(std::string c,std::string m,long s,unsigned r=0):std::runtime_error(m),code(std::move(c)),status(s),retry(r){}};
struct NetworkError:std::runtime_error{using std::runtime_error::runtime_error;};
struct Response { std::string bytes; long status; unsigned retry; bool rejectedFile=false; };
struct PackOutput { FILE* file; unsigned offset,total; std::atomic<unsigned>& progress; };
inline Response transferResponse(const std::string& service,const char* route,const std::string& body,const std::string& access,const std::atomic<bool>& cancel,const std::string& key,bool post,size_t maximum,long timeoutMs=20000L,PackOutput* output=nullptr,bool compressed=false){
    require(service.compare(0,8,"https://")==0&&body.size()<=20000,"Invalid account request");
    require(maximum<=(output?160u:8u)*1024*1024,"Account response limit exceeded");
    require(!output || (output->file && !post && !compressed && output->offset<output->total &&
        maximum==output->total-output->offset),"Invalid avatar pack range");
    struct Transfer {
        std::string bytes;
        const std::atomic<bool>* cancel;
        unsigned retry;
        size_t maximum,received;
        PackOutput* output;
        PackRangeHeaders fileHeaders;
        bool writeFailed=false;
    };
    Transfer transfer{{},&cancel,0,maximum,0,output,{output?output->offset:0,output?output->total:0}};
    struct Handles{CURL* curl=RBX::acquireVitaCurlConnection(RBX::VitaCurlChannel::Account);curl_slist* headers=nullptr;~Handles(){RBX::releaseVitaCurlConnection(curl,RBX::VitaCurlChannel::Account);if(headers)curl_slist_free_all(headers);}} handles;
    auto* curl=handles.curl;require(curl!=nullptr,"Account connection unavailable");
    auto option=[&](auto key,auto value){require(curl_easy_setopt(curl,key,value)==CURLE_OK,"Account HTTPS policy is unavailable.");};
    auto add=[&](const std::string& value){auto* list=curl_slist_append(handles.headers,value.c_str());require(list!=nullptr,"Account connection unavailable");handles.headers=list;};
    add("Content-Type: application/json");add("X-Client-Type: vita");
    if(compressed)option(CURLOPT_ACCEPT_ENCODING,"gzip");
    else add("Accept-Encoding: identity");
    if(!access.empty()){require(token(access),"Invalid account session");add("Authorization: Bearer "+access);}if(!key.empty())add("Idempotency-Key: "+key);
    auto url=service+route;RBX::Bloxicon::requireNetwork(url);
    option(CURLOPT_URL,url.c_str());option(CURLOPT_HTTPHEADER,handles.headers);
    option(CURLOPT_CONNECTTIMEOUT_MS,10000L);option(CURLOPT_TIMEOUT_MS,timeoutMs);
    if(output){
        option(CURLOPT_LOW_SPEED_LIMIT,128L);option(CURLOPT_LOW_SPEED_TIME,30L);
        if(output->offset){
            const auto range=std::to_string(output->offset)+"-";
            option(CURLOPT_RANGE,range.c_str());
        }
    }
    option(CURLOPT_NOSIGNAL,1L);option(CURLOPT_IPRESOLVE,long(CURL_IPRESOLVE_V4));
    option(CURLOPT_PROTOCOLS,long(CURLPROTO_HTTPS));option(CURLOPT_FOLLOWLOCATION,0L);option(CURLOPT_MAXREDIRS,0L);
    option(CURLOPT_SSL_VERIFYPEER,1L);option(CURLOPT_SSL_VERIFYHOST,2L);option(CURLOPT_SSLVERSION,long(CURL_SSLVERSION_TLSv1_2));
    const char* ca="ux0:/data/Bloxicon/system/certs/cacert.pem";FILE* f=std::fopen(ca,"rb");if(f)std::fclose(f);else ca="app0:/certs/cacert.pem";option(CURLOPT_CAINFO,ca);
    option(CURLOPT_HTTP_CONTENT_DECODING,compressed?1L:0L);option(CURLOPT_NOPROGRESS,0L);
    option(CURLOPT_XFERINFODATA,&transfer);option(CURLOPT_XFERINFOFUNCTION,+[](void* p,curl_off_t,curl_off_t,curl_off_t,curl_off_t)->int{return static_cast<Transfer*>(p)->cancel->load()?1:0;});
    option(CURLOPT_WRITEDATA,&transfer);option(CURLOPT_WRITEFUNCTION,+[](char* p,size_t a,size_t b,void* context)->size_t{
        auto* t=static_cast<Transfer*>(context);
        if(a&&b>t->maximum/a)return 0;
        const auto n=a*b;
        if(n>t->maximum-t->received||t->cancel->load())return 0;
        if(t->output){
            if(!t->fileHeaders.acceptsBody)return 0;
            if(std::fwrite(p,1,n,t->output->file)!=n){t->writeFailed=true;return 0;}
        }
        else try{t->bytes.append(p,n);}catch(...){return 0;}
        t->received+=n;
        if(t->output)t->output->progress.store(unsigned(t->received),std::memory_order_relaxed);
        return n;
    });
    option(CURLOPT_HEADERDATA,&transfer);option(CURLOPT_HEADERFUNCTION,+[](char* p,size_t a,size_t b,void* context)->size_t{
        if(a&&b>4096/a)return 0;const size_t n=a*b;
        auto* t=static_cast<Transfer*>(context);
        if(n>13&&strncasecmp(p,"Retry-After:",12)==0){unsigned value=0;size_t i=12;while(i<n&&(p[i]==' '||p[i]=='\t'))++i;while(i<n&&p[i]>='0'&&p[i]<='9'){value=std::min(3600u,value*10+unsigned(p[i++]-'0'));}t->retry=value;}
        if(t->output&&!t->fileHeaders.read({p,n}))return 0;
        return n;
    });
    if(post){option(CURLOPT_POST,1L);option(CURLOPT_POSTFIELDS,body.data());option(CURLOPT_POSTFIELDSIZE,long(body.size()));}
    require(!cancel.load(),"Account request cancelled");
    const CURLcode performed=RBX::performVitaHttps(curl,url,10000L);
    if(output && transfer.fileHeaders.rejected)
        return {{},transfer.fileHeaders.status,transfer.retry,true};
    if(performed!=CURLE_OK) {
        require(!transfer.writeFailed,"Could not write avatar pack. Check free storage.");
        recordTransportFailure(curl,performed,route,cancel.load());
        throw NetworkError("Connection interrupted. Retry, or continue offline.");
    }
    long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    return {std::move(transfer.bytes),status,transfer.retry};
}
inline Doc request(const std::string& service,const char* route,const std::string& body,const std::string& access,const std::atomic<bool>& cancel,const std::string& key="",bool post=false,size_t maximum=16384,long timeoutMs=20000L){
    require(maximum<=262144,"Account response limit exceeded");
    auto response=transferResponse(service,route,body,access,cancel,key,post,maximum,timeoutMs);
    auto result=parse(response.bytes);
    if(response.status!=200){std::string code="account_unavailable",message="The account service could not complete this request.";if(result.HasMember("error")&&result["error"].IsObject()){code=field(result["error"],"code");message=field(result["error"],"message");}require(code.size()<=64&&message.size()<=300,"Invalid account error");throw ApiError(code,message,response.status,response.retry);}return result;
}
inline bool schedule(std::function<void()> work){static RBX::ThreadPool pool(1,RBX::BaseThreadPool::WaitForRunningTasks,2);return pool.schedule([work](boost::shared_ptr<rbx::spin_mutex>){work();});}
inline bool scheduleCommunity(std::function<void()> work){static RBX::ThreadPool pool(1,RBX::BaseThreadPool::WaitForRunningTasks,1);return pool.schedule([work](boost::shared_ptr<rbx::spin_mutex>){work();});}
inline bool scheduleCatalogue(std::function<void()> work){static RBX::ThreadPool pool(1,RBX::BaseThreadPool::WaitForRunningTasks,1);return pool.schedule([work](boost::shared_ptr<rbx::spin_mutex>){work();});}
inline void preserveActiveOutfit(const std::string& account){
    require(uuid(account),"Account avatar marker needs recovery.");std::string active;
    require(read(VitaAvatar::profilePath(),16384,active,true),"Could not preserve the last account outfit.");
    if(active.empty())return;VitaAvatar::AccountReplica::canonical(active);
    auto backup="ux0:/data/Bloxicon/system/avatar/account-"+account+"-"+std::to_string(sceKernelGetSystemTimeWide())+".v1";
    require(write(backup,active),"Could not preserve the last account outfit; guest restoration paused.");
}
struct Session{std::string access,refresh,account,username,rotation;uint64_t until=0;int engineUserId=0;};
inline void tickEngineSocial();
inline VitaSession::Store& sessionStore(){
    static VitaSession::Store store("ux0:/data/Bloxicon/system/account-session.v1",
        [](const std::string& p,size_t maximum,std::string& bytes){return read(p,maximum,bytes,true);},
        [](const std::string& p,const std::string& bytes){return write(p,bytes);},
        [](const std::string& p){std::remove(p.c_str());},
        [](){SceKernelOpenPsId id={};require(sceKernelGetOpenPsId(&id)>=0,"Device sign-in storage is unavailable.");std::array<unsigned char,16> out;std::memcpy(out.data(),id.id,16);return out;});return store;
}
inline void rememberSession(const std::string& service,const Session& auth,uint64_t epoch){
    Doc d;d.SetObject();addString(d,"origin",service);addString(d,"refresh",auth.refresh);addString(d,"account",auth.account);addString(d,"username",auth.username);
    addString(d,"rotation",auth.rotation);
    d.AddMember("engine_user_id",auth.engineUserId,d.GetAllocator());
    sessionStore().save(epoch,json(d));
}
inline Session session(const Doc& d){Session s;s.access=field(d,"access_token");s.refresh=field(d,"refresh_token");require(token(s.access)&&token(s.refresh)&&d.HasMember("account")&&d["account"].IsObject()&&d.HasMember("expires_in")&&d["expires_in"].IsUint(),"Invalid account session");s.account=field(d["account"],"id");s.username=field(d["account"],"username");if(d["account"].HasMember("engine_user_id")){require(d["account"]["engine_user_id"].IsInt()&&RBX::BloxiconSocial::reservedId(d["account"]["engine_user_id"].GetInt()),"Invalid engine player identity.");s.engineUserId=d["account"]["engine_user_id"].GetInt();}require(uuid(s.account)&&s.username.size()>=3&&s.username.size()<=20,"Invalid account identity");s.until=sceKernelGetSystemTimeWide()+std::min(900u,d["expires_in"].GetUint())*1000000ull;return s;}
enum class SessionFailure { None, AccessRejected, RefreshRejected };
struct Reply{Session auth;std::string error,errorCode,code,uri,device,profile,submitted;uint64_t version=0,expires=0;unsigned interval=5,retry=30;bool conflict=false,pushed=false,remembered=false,rejectedLocal=false;int phase=0;SessionFailure sessionFailure=SessionFailure::None;};
class Connector{
    VitaBrowser::LoaderAsyncTask<Reply> task;
    Session auth;
    VitaAvatar::AccountReplica replica;
    std::string service,device,requestKey,pollKey,cache,deferredAvatar;
    std::string userCode,uri,notice="Connect your account to sync your outfit.";
    uint64_t due=0,expires=0,verifiedUntil=0,verifyAfter=0;unsigned interval=5;int phase=0;bool pending=false,conflict=false,readOnly=false,verifyRequested=false;
    unsigned short servicePort=31901;
    bool rememberRequired=false;uint64_t rememberAfter=0;
    void publishSession(){RBX::BloxiconSocial::setIdentity({auth.engineUserId,auth.account,auth.username});if(auth.access.empty()||auth.until<=sceKernelGetSystemTimeWide())return;auto host=service.substr(8);const auto colon=host.find(':');if(colon!=std::string::npos)host.resize(colon);RBX::Network::vitaSetOnlineSession(host,servicePort,auth.access,auth.until);}
    void persist(const VitaAvatar::AccountReplica& r){Doc d;d.SetObject();addString(d,"account",auth.account);d.AddMember("version",r.version,d.GetAllocator());addString(d,"base",r.base);addString(d,"local",r.local);d.AddMember("unbased_edit",r.unbasedEdit,d.GetAllocator());require(write(cache,json(d)),"Could not preserve the account outfit; synchronization paused.");}
    void bind(bool restoring=false){
        cache="ux0:/data/Bloxicon/system/avatar/account-"+digest(service+auth.account)+".json";replica={};deferredAvatar.clear();std::string bytes;
        require(read(cache,65536,bytes,true),"Could not read account outfit cache.");
        if(!bytes.empty()){
            auto d=parse(bytes);require(field(d,"account")==auth.account&&d.HasMember("version")&&d["version"].IsUint64(),"Invalid account outfit cache");
            replica.version=d["version"].GetUint64();
            replica.base=field(d,"base");replica.local=field(d,"local");
            if(d.HasMember("unbased_edit")){
                require(d["unbased_edit"].IsBool(),"Invalid account outfit cache");
                replica.unbasedEdit=d["unbased_edit"].GetBool();
            }
            if(replica.version||!replica.base.empty())replica.base=VitaAvatar::AccountReplica::canonical(replica.base);
            if(replica.version||replica.unbasedEdit||!replica.local.empty())replica.local=VitaAvatar::AccountReplica::canonical(replica.local);
        }
        std::string marker;require(read("ux0:/data/Bloxicon/system/avatar/account-active",80,marker,true),"Could not read the active outfit account.");
        require(!restoring||marker.empty()||marker==auth.account,"The saved outfit belongs to another account.");
        if(!restoring||marker.empty()){
            std::string guest;require(read(VitaAvatar::profilePath(),16384,guest,true),"Could not preserve guest outfit.");
            if(guest.empty())guest=VitaAvatar::encodeProfile(VitaAvatar::Profile());
            require(write("ux0:/data/Bloxicon/system/avatar/guest.v1",guest)&&write("ux0:/data/Bloxicon/system/avatar/account-active",auth.account),"Could not preserve guest outfit.");
        }
        if(!replica.local.empty())require(write(VitaAvatar::profilePath(),replica.local),"Could not select the account outfit.");
        phase=3;due=0;verifiedUntil=restoring?0:sceKernelGetSystemTimeWide()+30000000;publishSession();
        verifyRequested=restoring;notice=restoring?"Welcome back. Reconnecting your account...":"Signed in. Your Vita is ready for online play.";
    }
public:
    bool restoreSaved(){
        try{auto bytes=sessionStore().load();if(bytes.empty())return false;auto saved=parse(bytes);service=origin();require(field(saved,"origin")==service,"The saved sign-in belongs to another website.");
            Session restored;restored.refresh=field(saved,"refresh");restored.account=field(saved,"account");restored.username=field(saved,"username");
            if(saved.HasMember("engine_user_id")){require(saved["engine_user_id"].IsInt(),"Invalid saved player identity.");restored.engineUserId=saved["engine_user_id"].GetInt();require(!restored.engineUserId||RBX::BloxiconSocial::reservedId(restored.engineUserId),"Invalid saved player identity.");}
            if(saved.HasMember("rotation")){restored.rotation=field(saved,"rotation");require(restored.rotation.empty()||(restored.rotation.size()==48&&std::all_of(restored.rotation.begin(),restored.rotation.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c));})),"The saved refresh needs recovery.");}
            require(token(restored.refresh)&&uuid(restored.account)&&restored.username.size()>=3&&restored.username.size()<=20,"The saved sign-in needs recovery.");
            auth=restored;servicePort=gamePort();bind(true);return true;
        }catch(const std::exception& e){auth={};phase=0;notice=e.what();return false;}
    }
    bool connected()const{return phase==3&&!auth.account.empty();}
    bool onlineReady()const{auto now=sceKernelGetSystemTimeWide();return connected()&&auth.until>now+1000000&&verifiedUntil>now;}
    void verifyOnline(){if(connected()){verifyRequested=true;verifiedUntil=0;verifyAfter=0;notice="Checking your online session...";}}
    unsigned remainingSeconds()const{auto now=sceKernelGetSystemTimeWide();return phase==2&&expires>now?unsigned((expires-now+999999)/1000000):0;}
    bool busy()const{return pending||verifyRequested;}
    bool editable()const{return !connected()||!readOnly;}
    bool waiting()const{return phase==1||phase==2;}
    bool needsChoice()const{return conflict;}
    const std::string& message()const{return notice;}
    const std::string& code()const{return userCode;}
    const std::string& website()const{return uri;}
    const std::string& username()const{return auth.username;}
    const std::string& accountId()const{return auth.account;}
    int engineUserId()const{return auth.engineUserId;}
    // UI-thread snapshot only. Community workers never rotate refresh tokens.
    bool communitySession(std::string& endpoint,std::string& bearer){
        if(!connected())return false;
        if(auth.until<=sceKernelGetSystemTimeWide()+15000000){if(!verifyRequested)verifyOnline();return false;}
        endpoint=service;bearer=auth.access;return true;
    }
    void begin(){if(pending||connected())return;try{service=origin();servicePort=gamePort();requestKey=randomKey();pollKey=randomKey();phase=1;due=0;userCode.clear();uri.clear();notice="Requesting a sign-in code...";}catch(const std::exception& e){notice=e.what();}}
    void refreshAvatar(){if(connected()&&!readOnly&&!conflict)due=0;}
    void saved(const std::string& bytes){if(!connected())return;try{auto candidate=replica;candidate.local=VitaAvatar::AccountReplica::canonical(bytes);if(!candidate.version&&candidate.local!=replica.local)candidate.unbasedEdit=true;persist(candidate);replica=std::move(candidate);deferredAvatar.clear();due=sceKernelGetSystemTimeWide()+1000000;notice="Outfit saved on Vita. Syncing account...";}catch(const std::exception& e){notice=e.what();readOnly=true;}}
    void choose(bool local){if(pending)return;try{require(write(cache+".conflict-"+std::to_string(sceKernelGetSystemTimeWide()),replica.local),"Could not preserve the previous outfit.");auto selected=replica;selected.version=0;selected.base.clear();selected.unbasedEdit=false;if(!local)selected.local.clear();persist(selected);replica=std::move(selected);conflict=false;due=0;readOnly=false;}catch(const std::exception& e){notice=e.what();}}
    void disconnect(){
        RBX::BloxiconSocial::setIdentity({});
        std::string storageError;try{sessionStore().forget();}catch(const std::exception& e){storageError=e.what();}
        task.cancel();pending=false;auto old=auth;auto abandoned=device;auth={};phase=0;device.clear();userCode.clear();uri.clear();conflict=readOnly=verifyRequested=false;verifiedUntil=0;RBX::Network::vitaClearOnlineSession();
        rememberRequired=false;deferredAvatar.clear();
        notice="Disconnected on this Vita. Manage server sessions on the website if offline.";
        if(!storageError.empty())notice=storageError;
        try{if(!old.account.empty()){preserveActiveOutfit(old.account);std::string guest;require(read("ux0:/data/Bloxicon/system/avatar/guest.v1",16384,guest)&&!guest.empty(),"Guest outfit backup needs recovery.");VitaAvatar::AccountReplica::canonical(guest);require(write(VitaAvatar::profilePath(),guest),"Could not restore the guest outfit. The account look was kept.");std::remove("ux0:/data/Bloxicon/system/avatar/account-active");}}
        catch(const std::exception& e){notice=e.what();}
        if(!abandoned.empty()){auto s=service;schedule([s,abandoned]{std::atomic<bool> cancel(false);try{request(s,"/api/v1/device/cancel",object("device_code",abandoned),"",cancel,"",true);}catch(...){};});}
        if(!old.access.empty()){auto s=service;schedule([s,old]{std::atomic<bool> cancel(false);try{request(s,"/api/v1/logout","{}",old.access,cancel,"",true);}catch(...){};});}
    }
    // Called on the loader thread. HTTP never runs in this method or the renderer.
    void tick(const std::function<bool(const std::string&)>& apply={},bool canApply=true){
        tickEngineSocial();
        auto now=sceKernelGetSystemTimeWide();
        const uint64_t avatarPoll=apply?5000000:15000000;
        if(rememberRequired&&connected()&&now>=rememberAfter){try{rememberSession(service,auth,sessionStore().epoch());rememberRequired=false;}catch(const std::exception& e){notice=e.what();rememberAfter=now+5000000;}}
        if(connected()&&auth.until<=now+15000000&&!verifyRequested)verifyOnline();
        // A renderer that cannot accept a look yet must not trigger an HTTP
        // request every frame. Keep the durable replica and retry locally.
        if(!deferredAvatar.empty()){
            if(canApply){try{
                const bool selected=apply?apply(deferredAvatar):write(VitaAvatar::profilePath(),deferredAvatar);
                if(selected){deferredAvatar.clear();due=now+avatarPoll;notice="Account outfit is up to date.";}
            }catch(const std::exception& e){notice=e.what();readOnly=true;}}
            if(!deferredAvatar.empty()&&!verifyRequested)return;
        }
        if(pending){if(!canApply)return;Reply result;std::string error;auto state=task.poll(result,error);if(state==VitaBrowser::LoaderAsyncTask<Reply>::Pending)return;pending=false;
            if(state!=VitaBrowser::LoaderAsyncTask<Reply>::Ready){notice="Connection paused. Your local outfit was kept.";due=now+30000000;if(verifyRequested)verifyAfter=due;return;}
            if(!result.auth.account.empty()&&result.auth.account==auth.account){if(result.auth.refresh!=auth.refresh)rememberRequired=!result.remembered;auth=result.auth;if(result.sessionFailure==SessionFailure::None)publishSession();}
            if(!result.error.empty()){
                if(result.sessionFailure==SessionFailure::RefreshRejected){
                    disconnect();notice="Your session ended. Sign in again to play online.";return;
                }
                if(result.sessionFailure==SessionFailure::AccessRejected){
                    // A rejected bearer can be expired even while the local timer says it is valid.
                    // Keep the recovery token and ask for a fresh bearer before the next check.
                    auth.access.clear();auth.until=0;verifyOnline();
                    verifyAfter=now+uint64_t(result.retry)*1000000;
                    notice="Reconnecting your account...";return;
                }
                if(result.phase==2&&(result.errorCode=="expired_token"||result.errorCode=="invalid_device_code")){
                    phase=0;device.clear();userCode.clear();uri.clear();notice="This code expired. Request a new code.";return;
                }
                notice=result.error;due=now+uint64_t(result.retry)*1000000;if(result.phase==4)verifyAfter=due;return;
            }
            try{
                if(result.phase==1){device=result.device;userCode=result.code;uri=result.uri;interval=result.interval;expires=result.expires;phase=2;due=now+interval*1000000ull;notice="Enter this code on the website and approve the matching code.";}
                else if(result.phase==2){if(result.auth.account.empty()){interval=result.interval;due=now+interval*1000000ull;}else{auth=result.auth;bind();}}
                else if(result.phase==4){auth=result.auth;publishSession();verifyRequested=false;verifiedUntil=now+30000000;due=0;notice="Signed in. Your Vita is ready for online play.";}
                else if(result.phase==3){
                    auth=result.auth;if(result.conflict){conflict=true;notice="Another device saved a different look. Both outfits were kept.";return;}
                    auto candidate=replica;
                    if(result.rejectedLocal){
                        // Catalogue publication can lag a valid local item.
                        // Keep the selected outfit and durable dirty replica;
                        // replacing it with the website look makes equip appear
                        // to fail and can silently undo a user's choice.
                        if(candidate.local!=result.submitted){due=0;return;}
                        notice="Saved on this Vita. An item is not yet available for account sync.";
                        due=now+30000000;return;
                    }
                    else if(result.pushed)candidate.acknowledge(result.version,result.submitted,result.profile);
                    else if(candidate.dirty()&&candidate.local!=result.profile)return;else candidate.accept(result.version,result.profile);
                    if(candidate.version!=replica.version||candidate.local!=replica.local||candidate.base!=replica.base||candidate.unbasedEdit!=replica.unbasedEdit)persist(candidate);
                    replica=std::move(candidate);
                    if(apply){if(!apply(replica.local)){deferredAvatar=replica.local;notice="Account outfit is ready. Waiting for the current edit.";due=now+avatarPoll;return;}}
                    else{std::string current;require(read(VitaAvatar::profilePath(),16384,current,true),"Could not read the selected account outfit.");if(current!=replica.local)require(write(VitaAvatar::profilePath(),replica.local),"Could not select the saved account outfit.");}
                    notice=result.rejectedLocal?"Website outfit selected. The previous Vita outfit was backed up.":replica.dirty()?"Newer outfit edits waiting to sync.":"Account outfit is up to date.";due=now+(replica.dirty()?1000000:avatarPoll);
                }
            }catch(const std::exception& e){notice=e.what();readOnly=true;}return;
        }
        if(phase==2&&now>=expires){phase=0;device.clear();notice="Sign-in code expired. Connect again.";return;}
        if(!phase||(verifyRequested?now<verifyAfter:now<due)||((readOnly||conflict)&&!verifyRequested))return;
        auto p=verifyRequested?4:phase;auto s=service;auto grant=device;auto start=requestKey;auto poll=pollKey;auto a=auth;auto before=replica;auto wait=interval;auto epoch=sessionStore().epoch();
        pending=task.start(schedule,[p,s,grant,start,poll,a,before,wait,epoch](const std::atomic<bool>& cancel)mutable{Reply r;r.phase=p;
            bool usingAccess=false;
            try{
                if(p==1){RBX::Network::vitaPrepareOnlineTransport();auto started=sceKernelGetSystemTimeWide();auto d=request(s,"/api/v1/device/start","{}","",cancel,start,true);r.device=field(d,"device_code");r.code=field(d,"user_code");r.uri=field(d,"verification_uri");require(token(r.device)&&r.code.size()==11&&r.code[5]=='-'&&r.uri==s+"/device"&&d.HasMember("interval")&&d["interval"].IsUint()&&d.HasMember("expires_in")&&d["expires_in"].IsUint()&&d["expires_in"].GetUint()>0&&d["expires_in"].GetUint()<=600,"Invalid sign-in response");for(size_t i=0;i<r.code.size();++i)require(i==5||std::strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789",r.code[i]),"Invalid sign-in code");r.interval=std::max(5u,std::min(60u,d["interval"].GetUint()));r.expires=started+uint64_t(d["expires_in"].GetUint())*1000000;}
                else if(p==2){try{r.auth=session(request(s,"/api/v1/device/token",object("device_code",grant),"",cancel,poll,true));if(!cancel.load())rememberSession(s,r.auth,epoch);}catch(const ApiError& e){if(e.code!="authorization_pending"&&e.code!="slow_down")throw;r.interval=std::max(e.retry,e.code=="slow_down"?std::min(60u,wait+5):wait);}}
                else{
                    for(unsigned rotation=0;a.until<=sceKernelGetSystemTimeWide()+15000000&&rotation<2;++rotation){
                        // Commit the recovery proof before rotating. A power loss
                        // can then replay this exact exchange after the short
                        // ordinary retry window, without requesting another code.
                        if(a.rotation.empty())a.rotation=randomKey();r.auth=a;rememberSession(s,a,epoch);
                        Session fresh;for(unsigned attempt=0;;++attempt){try{fresh=session(request(s,"/api/v1/refresh",object("refresh_token",a.refresh),"",cancel,a.rotation,true));break;}catch(const NetworkError&){if(attempt||cancel.load())throw;}}
                        require(fresh.account==a.account,"Account changed; synchronization stopped.");r.auth=a=fresh;r.remembered=false;if(!cancel.load()){rememberSession(s,fresh,epoch);r.remembered=true;}
                    }
                    require(a.until>sceKernelGetSystemTimeWide()+15000000,"Account refresh is temporarily unavailable. Retry shortly.");
                    r.auth=a;
                    usingAccess=true;
                    if(p==4){auto state=request(s,"/api/v1/session","",a.access,cancel);require(state.HasMember("authenticated")&&state["authenticated"].IsBool(),"Invalid account response");if(!state["authenticated"].GetBool())throw ApiError("unauthenticated","Online session needs refresh.",401);require(state.HasMember("account")&&field(state["account"],"id")==a.account,"Account changed; synchronization stopped.");return r;}
                    auto d=request(s,"/api/v1/avatar/sync","",a.access,cancel);require(field(d,"account_id")==a.account&&d.HasMember("version")&&d["version"].IsUint64(),"Invalid account outfit response");r.version=d["version"].GetUint64();r.profile=VitaAvatar::AccountReplica::canonical(field(d,"profile"));auto decision=before.decide(r.version,r.profile);r.conflict=decision==VitaAvatar::AccountReplica::Conflict;
                    if(decision==VitaAvatar::AccountReplica::Push){
                        Doc body;body.SetObject();body.AddMember("version",r.version,body.GetAllocator());addString(body,"profile",before.local);
                        try{auto saved=request(s,"/api/v1/avatar/sync",json(body),a.access,cancel,"",true);require(field(saved,"account_id")==a.account&&saved.HasMember("version")&&saved["version"].IsUint64(),"Invalid account outfit response");r.version=saved["version"].GetUint64();r.profile=VitaAvatar::AccountReplica::canonical(field(saved,"profile"));r.submitted=before.local;r.pushed=true;}
                        catch(const ApiError& e){if(e.code!="item_unavailable")throw;r.submitted=before.local;r.rejectedLocal=true;}
                    }
                }
            }catch(const ApiError& e){
                r.error=e.what();r.errorCode=e.code;r.retry=std::max(5u,e.retry);
                if(e.status==401&&p>=3){
                    if(usingAccess)r.sessionFailure=SessionFailure::AccessRejected;
                    else if(e.code=="invalid_credentials"||e.code=="refresh_reused"||e.code=="unauthenticated")r.sessionFailure=SessionFailure::RefreshRejected;
                }
            }catch(const std::exception& e){r.error=e.what();r.retry=std::min(60u,std::max(5u,wait*2));}return r;
        });
        if(!pending){notice="Account worker is busy. Retry shortly.";due=now+30000000;if(verifyRequested)verifyAfter=due;}
    }
};
inline Connector& connector(){static Connector value;return value;}
#include "VitaEngineSocial.inl"
inline void recoverGuest(){if(connector().restoreSaved())return;std::string marker,guest;if(read("ux0:/data/Bloxicon/system/avatar/account-active",80,marker,true)&&!marker.empty()){preserveActiveOutfit(marker);require(read("ux0:/data/Bloxicon/system/avatar/guest.v1",16384,guest)&&!guest.empty(),"Guest outfit backup needs recovery.");VitaAvatar::AccountReplica::canonical(guest);require(write(VitaAvatar::profilePath(),guest),"Could not restore guest outfit.");std::remove("ux0:/data/Bloxicon/system/avatar/account-active");}}
}
