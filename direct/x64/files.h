#pragma once
#include <windows.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Linux x86_64 stat (not the arm64 layout and not Windows' CRT stat).
struct LinuxStat {
    uint64_t dev,ino,nlink; uint32_t mode,uid,gid,pad; uint64_t rdev;
    int64_t size,blksize,blocks,atime,atime_ns,mtime,mtime_ns,ctime,ctime_ns,reserved[3];
};
static_assert(sizeof(LinuxStat)==144 && offsetof(LinuxStat,size)==48);
struct NativeFile { HANDLE handle=INVALID_HANDLE_VALUE; std::string path; uint64_t offset=0; std::shared_ptr<std::string> contents; uint32_t descriptor_flags=0; uint64_t flags=0; };
class ReadOnlyFiles {
    std::filesystem::path root;
    std::map<std::string,std::string> links;
    std::map<std::string,std::filesystem::path> mounts;
    std::map<int,NativeFile> descriptors;
    std::map<std::string,std::shared_ptr<std::string>> virtual_files;
    int next=3;
public:
    void init(const std::filesystem::path& sysroot);
    void mount(const std::string& guest,const std::filesystem::path& host);
    void text(const std::string& guest,std::string contents);
    std::string resolve(std::string path,bool follow=true) const;
    std::filesystem::path host(const std::string& path) const;
    std::string readlink(const std::string& path) const;
    int open(const std::string& path,uint64_t flags);
    int close(int fd);
    bool writable(const std::string& path) const;
    int mkdir(const std::string& path);
    int rename(const std::string& from,const std::string& to,bool no_replace);
    int unlink(const std::string& path,bool directory);
    int truncate(int fd,uint64_t length);
    int sync(int fd);
    int64_t write(int fd,const void* data,uint64_t size);
    NativeFile* get(int fd);
    int64_t read(int fd,void* data,uint64_t size,uint64_t offset,bool advance);
    int64_t getdents(int fd,void* data,uint64_t size);
    int stat(int fd,LinuxStat& result);
    int stat(const std::string& path,LinuxStat& result);
    ~ReadOnlyFiles();
};
