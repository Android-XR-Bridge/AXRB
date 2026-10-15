#include "../files.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void require(bool value){if(!value)throw std::runtime_error("save regression failed");}
int main(){
    auto root=std::filesystem::temp_directory_path()/(L"axrb-save-test-"+std::to_wstring(GetCurrentProcessId()));
    // This executable exclusively owns this new, per-process temporary directory.
    if(!std::filesystem::create_directory(root))return 2;
    try {
        std::filesystem::create_directories(root/L"apex");
        std::filesystem::create_directories(root/L"data/data/test");
        std::ofstream(root/L".symlinks.json")<<"{}";
        const char* saved="/data/data/test/preferences.xml";
        const char* backup="/data/data/test/preferences.xml.bak";
        {
            ReadOnlyFiles files;files.init(root);
            SetEnvironmentVariableW(L"AXRB_X64_WRITABLE_DATA",nullptr);
            require(files.open(saved,0x41)==-30);
            SetEnvironmentVariableW(L"AXRB_X64_WRITABLE_DATA",L"1");
            require(!files.writable("/data/local/tmp/dex"));
            require(!files.writable("/data/data/../../../system/lib64/code.so"));
            int fd=files.open(saved,0x41);require(fd>=0);
            require(files.write(fd,"original",8)==8);require(files.sync(fd)==0);require(files.close(fd)==0);
            require(files.rename(saved,backup,false)==0);
            fd=files.open(saved,0x41);require(fd>=0);
            require(files.write(fd,"replacement",11)==11);require(files.truncate(fd,7)==0);
            require(files.sync(fd)==0);require(files.close(fd)==0);
            require(files.rename(backup,saved,true)==-17);
            require(files.unlink(backup,false)==0);
            require(files.mkdir("/data/data/test/nested")==0);
            require(files.unlink("/data/data/test/nested",true)==0);
        }
        {
            ReadOnlyFiles files;files.init(root);char bytes[16]{};
            int fd=files.open(saved,0);require(fd>=0);
            require(files.read(fd,bytes,sizeof bytes,0,true)==7);
            require(std::string(bytes,7)=="replace");require(files.close(fd)==0);
        }
        std::filesystem::remove_all(root);
        std::cout<<"NATIVE_X64_SAVE_PASS atomic preference replacement and reopen\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"; artifacts: "<<root<<'\n';return 1;}
}
