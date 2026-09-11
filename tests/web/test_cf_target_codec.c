/* Public fixture only. Exercise the unchanged target decoder and lifecycle.
 * Host open normally binds to host statfs. Replace only that hardware input
 * with the explicit test MAC/UUID; never copy the binding from the record. */
#define WEB_HOST_TEST 1
#include "web/rng_nv.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"codec check failed: line %d\n",__LINE__); return 1; } } while (0)
static void fixture_binding(void)
{
    const char material[]="4vrs-nv-binding-v1:00:90:e8:1f:4c:f1";
#ifdef CF_CODEC_CONFIRMED_UUID
    const unsigned char uuid[16]={0x2a,0xd4,0xf1,0x7a,0x0e,0xb1,0xbc,0x47,0x8c,0x4e,0xb2,0x3d,0x70,0xf3,0x9b,0x52};
#else
    const unsigned char uuid[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
#endif
    memset(binding,0,sizeof(binding));
    mbedtls_sha256((const unsigned char *)material,sizeof(material)-1,binding,0);
    memcpy(binding+32,uuid,16);
}
int main(int argc,char **argv)
{
    unsigned char output[32];
    CHECK(argc==2);
    CHECK(rng_nv_open(argv[1])==0);fixture_binding();
    CHECK(rng_nv_status()==0 && rng_nv_generation()==1);
    CHECK(rng_nv_policy()!=0); /* Bootstrap must not authorize trial policy. */
    CHECK(rng_nv_random(output,sizeof(output))!=0);
    CHECK(rng_nv_start()==0 && rng_nv_generation()==2);
    CHECK(rng_nv_status()==0 && rng_nv_generation()==2);
    CHECK(rng_nv_random(output,sizeof(output))==0);
    rng_nv_close();
    CHECK(rng_nv_open(argv[1])==0);fixture_binding();
    CHECK(rng_nv_start()==0 && rng_nv_generation()==3);
    CHECK(rng_nv_random(output,sizeof(output))==0);
    rng_nv_close();
    CHECK(rng_nv_open(argv[1])==0);fixture_binding();binding[0]^=1;
    CHECK(rng_nv_status()==4);
    CHECK(rng_nv_start()!=0 && rng_nv_random(output,sizeof(output))!=0);
    rng_nv_close();mbedtls_platform_zeroize(output,sizeof(output));
    puts("PASS unchanged target codec: generation 1 -> 2 -> 3; wrong binding refused; policy absent");
    return 0;
}
