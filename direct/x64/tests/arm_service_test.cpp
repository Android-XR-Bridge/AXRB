#include "../arm_service.h"
#include <cstdio>
#include <initializer_list>
#include <windows.h>
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    AxrbArmServiceConfig config{sizeof(config),1,argv[1],argv[2],argv[3]};
    if(axrb_arm_service_start(&config)){fprintf(stderr,"ARM_SERVICE_FAIL %s\n",axrb_arm_service_error());return 1;}
    for(const char* name:{"libmain.so","libunity.so"}) {
        auto handle=axrb_arm_service_load(name);
        if(!handle){fprintf(stderr,"ARM_SERVICE_LOAD_FAIL %s: %s\n",name,axrb_arm_service_error());return 1;}
        auto symbol=axrb_arm_service_symbol(handle,"JNI_OnLoad");
        if(!symbol){fprintf(stderr,"ARM_SERVICE_SYMBOL_FAIL %s: %s\n",name,axrb_arm_service_error());return 1;}
        printf("ARM_SERVICE_LIBRARY_READY %s JNI_OnLoad=%llx\n",name,(unsigned long long)symbol);
    }
    puts("ARM_SERVICE_LOAD_PASS real ARM linker/Bionic/TLS and Unity library constructors");return 0;
}
