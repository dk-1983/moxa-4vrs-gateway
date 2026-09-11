#include "installer/install_package.h"
#include <stdio.h>
int main(int argc,char**argv){install_package_t p;const char*stage;int r;if(argc!=2)return 2;r=install_package_read(argv[1],&p,&stage);printf("%s\n",stage);if(!r)install_package_free(&p);return r?1:0;}
