#define main original_main
#include "test_gateway_panel.c"
#undef main
int main(void){setvbuf(stdout,0,_IONBF,0);puts("BEGIN held_keys");held_keys();printf("checks=%u failed=%u\n",checks,failures);return failures!=0;}
