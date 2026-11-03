#pragma once

// Used only by VitaSocketPolicy. No blocking socket operations or credential
// files: all socket callbacks return WANT_READ/WRITE to the existing pump.
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <openssl/rand.h>
#include <memory>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>

namespace RBX { namespace Network { namespace OnlineTls {
inline bool pending(int result) {
    if(result==MBEDTLS_ERR_SSL_WANT_READ || result==MBEDTLS_ERR_SSL_WANT_WRITE)return true;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    // TLS 1.3 tickets are successfully processed control messages. The next
    // nonblocking pump must continue reading application data on this stream.
    if(result==MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)return true;
#endif
#ifdef MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS
    if(result==MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS)return true;
#endif
#ifdef MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS
    if(result==MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS)return true;
#endif
    return false;
}
struct Config {
    mbedtls_ssl_config config;
    mbedtls_x509_crt certificates;
    Config() {
        mbedtls_ssl_config_init(&config);mbedtls_x509_crt_init(&certificates);
    }
    ~Config(){mbedtls_ssl_config_free(&config);mbedtls_x509_crt_free(&certificates);}
    void initialize(const char* ca) {
        if(mbedtls_x509_crt_parse_file(&certificates,ca)!=0 ||
           mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)
            throw std::runtime_error("Online certificate store is unavailable.");
        mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config,&certificates,nullptr);
        mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_rng(&config,[](void*,unsigned char* out,size_t count)->int {
            return count<=16384 && RAND_bytes(out,int(count))==1 ? 0 : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        },nullptr);
    }
};
class Stream {
    std::shared_ptr<Config> config;
    mbedtls_ssl_context ssl;
    int socket;
    std::string hello;
    unsigned char reply[5]{};
    size_t sent=0,received=0;
    unsigned pendingWrite=0;
    std::array<unsigned char,16384> pendingBytes;
    bool handshake=false,accepted=false;
    static bool again(int result) {
        const int code=result==-1?*sceNetErrnoLoc():result;
        // Sony's native sockets return the full negative 0x804101xx value;
        // Newlib-style adapters instead return -1 and a positive errno.
        // Both represent a pending nonblocking operation, never a TLS failure.
        return code==SCE_NET_EWOULDBLOCK || code==int(SCE_NET_ERROR_EWOULDBLOCK) ||
               code==SCE_NET_EINPROGRESS || code==int(SCE_NET_ERROR_EINPROGRESS) ||
               code==SCE_NET_EALREADY || code==int(SCE_NET_ERROR_EALREADY) ||
               code==SCE_NET_ENOTCONN || code==int(SCE_NET_ERROR_ENOTCONN) ||
               code==SCE_NET_EINTR || code==int(SCE_NET_ERROR_EINTR);
    }
    void failure(const char* phase,int error){
        failed=true;
        std::fprintf(stderr,"Vita TLS %s failed code=%d verify=%lu\n",phase,error,
            static_cast<unsigned long>(mbedtls_ssl_get_verify_result(&ssl)));
    }
public:
    bool failed=false;
    Stream(std::shared_ptr<Config> value,int fd,const std::string& host,const std::string& bearer)
        :config(std::move(value)),socket(fd),hello("RBXA\1"+bearer) {
        mbedtls_ssl_init(&ssl);
        if(mbedtls_ssl_setup(&ssl,&config->config)!=0 || mbedtls_ssl_set_hostname(&ssl,host.c_str())!=0){failed=true;return;}
        mbedtls_ssl_set_bio(&ssl,this,
            [](void* context,const unsigned char* bytes,size_t count)->int {
                auto* self=static_cast<Stream*>(context);
                int n=sceNetSend(self->socket,bytes,unsigned(count),SCE_NET_MSG_DONTWAIT);
                return n>=0?n:again(n)?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            },[](void* context,unsigned char* bytes,size_t count)->int {
                auto* self=static_cast<Stream*>(context);
                int n=sceNetRecv(self->socket,bytes,unsigned(count),SCE_NET_MSG_DONTWAIT);
                return n>=0?n:again(n)?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            },nullptr);
    }
    ~Stream(){std::fill(hello.begin(),hello.end(),0);mbedtls_ssl_free(&ssl);}
    bool progress() {
        if(failed)return false;
        if(!handshake){
            const int n=mbedtls_ssl_handshake(&ssl);
            if(n!=0){if(!pending(n))failure("handshake",n);return false;}
            if(mbedtls_ssl_get_verify_result(&ssl)!=0){failure("certificate",0);return false;}
            handshake=true;
        }
        if(sent<hello.size()){
            const int n=mbedtls_ssl_write(&ssl,reinterpret_cast<const unsigned char*>(hello.data())+sent,hello.size()-sent);
            if(n<=0){if(!pending(n))failure("admission-write",n);return false;}
            sent+=size_t(n);if(sent<hello.size())return false;
        }
        if(!accepted){
            const int n=mbedtls_ssl_read(&ssl,reply+received,sizeof(reply)-received);
            if(n<=0){if(!pending(n))failure("admission-read",n);return false;}
            received+=size_t(n);if(received<sizeof(reply))return false;
            if(std::memcmp(reply,"RBXO\1",5)!=0){failed=true;return false;}
            std::fill(hello.begin(),hello.end(),0);hello.clear();accepted=true;
        }
        return true;
    }
    int write(const void* data,unsigned size){
        // The tunnel may compact its queue between pumps. Keep the exact write
        // buffer and length stable across mbedTLS WANT_READ/WRITE retries.
        if(!pendingWrite){pendingWrite=std::min(size,16384u);std::memcpy(pendingBytes.data(),data,pendingWrite);}
        const int result=mbedtls_ssl_write(&ssl,pendingBytes.data(),pendingWrite);
        if(result>0 || !pending(result))pendingWrite=0;
        return result;
    }
    int read(void* data,unsigned size){return mbedtls_ssl_read(&ssl,static_cast<unsigned char*>(data),size);}
};
} } }
