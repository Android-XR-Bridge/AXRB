#include "files.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string normal(const std::string& path) {
    if(path.empty() || path[0]!='/' || path.find_first_of("\\:")!=std::string::npos) throw std::runtime_error("Expected an absolute Linux path");
    std::vector<std::string> parts;
    std::istringstream input(path); std::string part;
    while(std::getline(input,part,'/')) {
        if(part.empty() || part==".") continue;
        if(part=="..") { if(!parts.empty()) parts.pop_back(); }
        else parts.push_back(part);
    }
    std::string result;
    for(const auto& p:parts) result+="/"+p;
    return result.empty()?"/":result;
}
int error() {
    switch(GetLastError()) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return -2;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return -13;
    case ERROR_INVALID_HANDLE:return -9;
    case ERROR_ALREADY_EXISTS:case ERROR_FILE_EXISTS:return -17;
    case ERROR_DIR_NOT_EMPTY:return -39;
    case ERROR_DISK_FULL:return -28;
    default:return -5;
    }
}
}
void ReadOnlyFiles::init(const std::filesystem::path& sysroot) {
    root=std::filesystem::canonical(sysroot);
    std::ifstream input(root/L".symlinks.json");
    char punctuation;
    if(!(input>>punctuation) || punctuation!='{') throw std::runtime_error("Missing sysroot symlink manifest");
    while(input>>std::ws && input.peek()!='}') {
        std::string from,to;
        if(input.peek()!='"' || !(input>>std::quoted(from)>>punctuation) || punctuation!=':' || !(input>>std::ws) || input.peek()!='"' || !(input>>std::quoted(to)))
            throw std::runtime_error("Invalid symlink manifest");
        links.emplace(normal(from),to);
        input>>std::ws;
        if(input.peek()=='}') break;
        if(!(input>>punctuation) || punctuation!=',') throw std::runtime_error("Invalid symlink separator");
    }
    if(!(input>>punctuation) || punctuation!='}') throw std::runtime_error("Truncated symlink manifest");
    std::string search="/apex/com.android.runtime/${LIB}/bionic:/apex/com.android.art/${LIB}:/system/${LIB}";
    // NativeLoader falls back to default when system is absent. An intermediate
    // system -> default link is not transitive for app namespace dependencies
    // and would load a second libc with an incompatible initial-exec TLS block.
    std::vector<std::string> namespaces;
    for(const auto& entry:std::filesystem::directory_iterator(root/L"apex")) if(entry.is_directory()) {
        auto name=entry.path().filename().string();search+=":/apex/"+name+"/${LIB}";
        std::replace(name.begin(),name.end(),'.','_');namespaces.push_back(name);
    }
    std::string config="dir.axrb=/apex\ndir.axrb=/data\ndir.axrb=/system\n[axrb]\nnamespace.default.isolated=false\nnamespace.default.visible=true\nnamespace.default.search.paths="+search+"\nadditional.namespaces=";
    for(size_t i=0;i<namespaces.size();i++) config+=(i?",":"")+namespaces[i];
    config+='\n';
    for(const auto& name:namespaces) {
        auto prefix="namespace."+name;
        config+=prefix+".isolated=false\n"+prefix+".visible=true\n"+prefix+".links=default\n"+prefix+".link.default.allow_all_shared_libs=true\n";
    }
    text("/linkerconfig/ld.config.txt",std::move(config));
}
void ReadOnlyFiles::mount(const std::string& guest,const std::filesystem::path& native) { mounts[normal(guest)]=std::filesystem::canonical(native); }
void ReadOnlyFiles::text(const std::string& guest,std::string contents) { virtual_files[normal(guest)]=std::make_shared<std::string>(std::move(contents)); }
std::string ReadOnlyFiles::resolve(std::string path,bool follow) const {
    path=normal(path);
    for(int count=0;count<40;count++) {
        bool changed=false;
        for(size_t at=1;at<=path.size();at++) if(at==path.size() || path[at]=='/') {
            if(!follow && at==path.size()) break;
            auto prefix=path.substr(0,at);auto it=links.find(prefix);
            if(it==links.end()) continue;
            std::string target=it->second;
            if(target.empty()) throw std::runtime_error("Empty symlink");
            if(target[0]!='/') target=prefix.substr(0,prefix.rfind('/')+1)+target;
            path=normal(target+path.substr(at));changed=true;break;
        }
        if(!changed) return path;
    }
    throw std::runtime_error("Symlink loop");
}
std::filesystem::path ReadOnlyFiles::host(const std::string& path) const {
    auto resolved=resolve(path);
    auto mounted=mounts.find(resolved);
    if(mounted!=mounts.end()) return mounted->second;
    auto utf8=std::u8string(reinterpret_cast<const char8_t*>(resolved.data()+1),resolved.size()-1);
    auto candidate=std::filesystem::weakly_canonical(root/std::filesystem::path(utf8));
    auto relative=candidate.lexically_relative(root);
    if(relative.empty() || *relative.begin()==L"..") throw std::runtime_error("Path escaped read-only sysroot");
    return candidate;
}
std::string ReadOnlyFiles::readlink(const std::string& path) const {
    auto it=links.find(resolve(path,false));return it==links.end()?std::string():it->second;
}
bool ReadOnlyFiles::writable(const std::string& path) const {
    if(!GetEnvironmentVariableW(L"AXRB_X64_WRITABLE_DATA",nullptr,0))return false;
    try {auto p=resolve(path);if(p.rfind("/data/data/",0)!=0 && p.rfind("/data/user/",0)!=0 && p.rfind("/data/user_de/",0)!=0 && p.rfind("/storage/",0)!=0)return false;
        if(mounts.count(p))return false;
        auto relative=host(p).lexically_relative(root);
        return !relative.empty() && (*relative.begin()==L"data" || *relative.begin()==L"storage");
    }catch(...){return false;}
}
int ReadOnlyFiles::mkdir(const std::string& path) {
    if(!writable(path))return -30;
    try{return CreateDirectoryW(host(path).c_str(),nullptr)?0:error();}catch(...){return -2;}
}
int ReadOnlyFiles::rename(const std::string& from,const std::string& to,bool no_replace) {
    if(!writable(from)||!writable(to))return -30;
    try {return MoveFileExW(host(from).c_str(),host(to).c_str(),no_replace?0:MOVEFILE_REPLACE_EXISTING)?0:error();}catch(...){return -2;}
}
int ReadOnlyFiles::unlink(const std::string& path,bool directory) {
    if(!writable(path))return -30;
    try {auto target=host(path);return (directory?RemoveDirectoryW(target.c_str()):DeleteFileW(target.c_str()))?0:error();}catch(...){return -2;}
}
int ReadOnlyFiles::truncate(int fd,uint64_t length) {
    auto* file=get(fd);if(!file || !(file->flags&3))return -9;
    if(length>INT64_MAX)return -22;
    LARGE_INTEGER size;size.QuadPart=static_cast<LONGLONG>(length);
    return SetFilePointerEx(file->handle,size,nullptr,FILE_BEGIN)&&SetEndOfFile(file->handle)?0:error();
}
int ReadOnlyFiles::sync(int fd) {
    auto* file=get(fd);if(!file)return -9;
    if(!(file->flags&3))return 0;
    return FlushFileBuffers(file->handle)?0:error();
}
int64_t ReadOnlyFiles::write(int fd,const void* data,uint64_t size) {
    auto* file=get(fd);if(!file || !(file->flags&3))return -9;
    if(size>MAXDWORD)return -22;
    LARGE_INTEGER position;position.QuadPart=static_cast<LONGLONG>(file->offset);
    if(!SetFilePointerEx(file->handle,position,nullptr,(file->flags&0x400)?FILE_END:FILE_BEGIN))return error();
    DWORD done=0;if(!WriteFile(file->handle,data,static_cast<DWORD>(size),&done,nullptr))return error();
    position.QuadPart=0;SetFilePointerEx(file->handle,position,&position,FILE_CURRENT);file->offset=position.QuadPart;
    return done;
}
int ReadOnlyFiles::open(const std::string& path,uint64_t flags) {
    // App-data writes are opt-in; framework and staged code remain read-only.
    bool writing=(flags&(3|0x40|0x200|0x400))!=0;
    if(writing && !writable(path))return -30;
    if((flags&3)==3 || flags & ~(0x80000ull|0x10000ull|0x20000ull|0x8000ull|0x800ull|0x200000ull|3|0x40|0x80|0x200|0x400)) return -22;
    try {
        if((flags&0x20000) && !readlink(path).empty()) return -40;
        auto resolved=resolve(path);
        if(auto it=virtual_files.find(resolved);it!=virtual_files.end()) {
            int fd=next++;descriptors.emplace(fd,NativeFile{INVALID_HANDLE_VALUE,resolved,0,it->second});return fd;
        }
        auto target=host(path);
        DWORD disposition=(flags&0x40)?((flags&0x80)?CREATE_NEW:(flags&0x200)?CREATE_ALWAYS:OPEN_ALWAYS):(flags&0x200)?TRUNCATE_EXISTING:OPEN_EXISTING;
        DWORD rights=(flags&3)==1?GENERIC_WRITE:(flags&3)==2?GENERIC_READ|GENERIC_WRITE:GENERIC_READ;
        HANDLE handle=CreateFileW(target.c_str(),rights,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,disposition,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(handle==INVALID_HANDLE_VALUE) return error();
        BY_HANDLE_FILE_INFORMATION info{};
        if((flags&0x10000) && (!GetFileInformationByHandle(handle,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))) { CloseHandle(handle);return -20; }
        int fd=next++;descriptors.emplace(fd,NativeFile{handle,resolve(path),0,{},static_cast<uint32_t>((flags&0x80000)?1:0),flags});return fd;
    } catch(...) { return -2; }
}
NativeFile* ReadOnlyFiles::get(int fd) { auto it=descriptors.find(fd);return it==descriptors.end()?nullptr:&it->second; }
int ReadOnlyFiles::close(int fd) { auto it=descriptors.find(fd);if(it==descriptors.end()) return -9;if(it->second.handle!=INVALID_HANDLE_VALUE) CloseHandle(it->second.handle);descriptors.erase(it);return 0; }
int64_t ReadOnlyFiles::read(int fd,void* data,uint64_t size,uint64_t offset,bool advance) {
    auto* file=get(fd);if(!file) return -9;
    if(size>MAXDWORD || offset>INT64_MAX) return -22;
    if(file->contents) {
        auto at=advance?file->offset:offset;auto& text=*file->contents;
        auto length=at>=text.size()?0:std::min<uint64_t>(size,text.size()-at);
        if(length) std::memcpy(data,text.data()+at,static_cast<size_t>(length));
        if(advance) file->offset+=length;
        return length;
    }
    LARGE_INTEGER position;position.QuadPart=static_cast<LONGLONG>(advance?file->offset:offset);
    if(!SetFilePointerEx(file->handle,position,nullptr,FILE_BEGIN)) return error();
    DWORD done=0;if(!ReadFile(file->handle,data,static_cast<DWORD>(size),&done,nullptr)) return error();
    if(advance) file->offset+=done;
    return done;
}
int ReadOnlyFiles::stat(int fd,LinuxStat& result) {
    auto* file=get(fd);if(!file) return -9;
    if(file->contents) {
        result={};result.dev=0x41585242;result.ino=std::hash<std::string>{}(file->path);result.nlink=1;result.mode=0100444;
        result.uid=result.gid=file->path=="/dev/__properties__"?0:10000;result.size=file->contents->size();result.blksize=4096;result.blocks=(result.size+511)/512;return 0;
    }
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file->handle,&info)) return error();
    result={};result.dev=info.dwVolumeSerialNumber;result.ino=(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
    result.nlink=info.nNumberOfLinks;result.mode=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?0040555:0100444;
    if(writable(file->path))result.mode|=0222;
    result.uid=result.gid=file->path=="/dev/__properties__"?0:10000;result.size=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;result.blksize=4096;result.blocks=(result.size+511)/512;
    auto time=[](FILETIME t,int64_t& sec,int64_t& ns) { int64_t value=static_cast<int64_t>((uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime)-116444736000000000LL;sec=value/10000000;ns=value%10000000*100; };
    time(info.ftLastAccessTime,result.atime,result.atime_ns);time(info.ftLastWriteTime,result.mtime,result.mtime_ns);time(info.ftCreationTime,result.ctime,result.ctime_ns);
    return 0;
}
int ReadOnlyFiles::stat(const std::string& path,LinuxStat& result) { int fd=open(path,0);if(fd<0) return fd;int rc=stat(fd,result);close(fd);return rc; }
int64_t ReadOnlyFiles::getdents(int fd,void* data,uint64_t size) {
    auto* file=get(fd);if(!file)return -9;
    LinuxStat info{};if(stat(fd,info) || (info.mode&0170000)!=0040000)return -20;
    try {
        std::vector<std::pair<std::string,uint8_t>> names{{".",4},{"..",4}};
        for(auto& entry:std::filesystem::directory_iterator(host(file->path))) {
            auto utf8=entry.path().filename().u8string();std::string name(reinterpret_cast<const char*>(utf8.data()),utf8.size());
            names.emplace_back(std::move(name),entry.is_directory()?4:8);
        }
        std::sort(names.begin()+2,names.end());uint64_t done=0;
        for(auto at=file->offset;at<names.size();at++) {
            const auto& [name,type]=names[at];auto length=(19+name.size()+1+7)&~size_t(7);
            if(length>size-done)return done?static_cast<int64_t>(done):-22;
            auto* record=static_cast<uint8_t*>(data)+done;std::memset(record,0,length);
            uint64_t inode=std::hash<std::string>{}(file->path+"/"+name),next=at+1;auto bytes=static_cast<uint16_t>(length);
            std::memcpy(record,&inode,8);std::memcpy(record+8,&next,8);std::memcpy(record+16,&bytes,2);record[18]=type;
            std::memcpy(record+19,name.c_str(),name.size()+1);done+=length;file->offset=next;
        }
        return static_cast<int64_t>(done);
    }catch(...){return -5;}
}
ReadOnlyFiles::~ReadOnlyFiles() { for(auto& [fd,file]:descriptors) if(file.handle!=INVALID_HANDLE_VALUE) CloseHandle(file.handle); }
