#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace RBX { namespace BloxiconSocial {
struct Identity { int userId=0; std::string account, username; };
struct Relation { int userId=0, status=0; std::string username; };
enum Kind { Profile, Lookup, Change, LookupName };
struct Result { int userId=0, status=0; std::string username, event, error; std::vector<Relation> relationships; };
struct Request { Kind kind=Profile; uint64_t generation=0; int userId=0; std::string action,name; std::function<void(const Result&)> done; };
bool reservedId(int id);
void setIdentity(const Identity& identity);
Identity identity();
bool knownIdentity(int id);
// -1 means this pair belongs to another provider; 0 means still loading.
int friendStatus(int source,int target);
bool query(int source,int target,std::function<void(const Result&)> done);
bool change(int source,int target,bool makeFriends,std::function<void(const Result&)> done);
bool lookup(int userId,const std::string& name,std::function<void(const Result&)> done);
bool friends(int userId,std::function<void(const Result&)> done);
std::string notice();
void refresh();
void invalidate();
bool take(Request& request);
void finish(Request request,Result result);
} }
