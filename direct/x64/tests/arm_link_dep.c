volatile long dep_value;
__attribute__((constructor)) static void initialize(void){dep_value=41;}
long dep_answer(long input){return input+dep_value;}

