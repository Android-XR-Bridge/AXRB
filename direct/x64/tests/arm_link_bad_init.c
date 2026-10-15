__attribute__((constructor)) static void initialize(void){__asm__ volatile("svc #0");}
int unused_export(void){return 1;}
