#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/* stubs */

void con_hex(const char*a,uint64_t b){}
void con_str(const char*a,const char*b){}
static uint64_t g_lo,g_hi;
int fdt_chosen_u64(uintptr_t dtb,const char*n,uint64_t*o){ *o = strstr(n,"start")? g_lo:g_hi; return 1;}
#include "loader_part.c"
int main(void){
  size_t N=1<<20; int bad=0;
  for(int off=0; off<24; off++){
    uint8_t *buf=aligned_alloc(4096,N+4096); memset(buf,0,N+4096);
    srand(off); for(size_t i=0;i<N;i++) buf[i]=rand();
    size_t at = 5000+off; /* payload start, any alignment */
    uint8_t pl[1024]; for(int i=0;i<1024;i++) pl[i]=i*7;
    memcpy(pl,"SWEETXNU",8); pl[8]=1;pl[9]=0;pl[10]=pl[11]=0;
    memcpy(buf+at,pl,1024);
    g_lo=(uint64_t)buf; g_hi=(uint64_t)buf+N;
    uint64_t hi; uintptr_t a=locate(0,&hi);
    if(a!=(uintptr_t)buf+at){printf("off %d: a=%lx buf=%lx expect +%lx\n",off,a,(uintptr_t)buf,at);bad++;continue;}
    uintptr_t b=realign(a,hi);
    if(b!=(a&~7UL)||memcmp((void*)b,pl,1024)){printf("off %d realign bad\n",off);bad++;}
    if(!loader_payload_present(0)) {printf("off %d present bad\n",off);bad++;}
    free(buf);
  }
  printf("bad=%d\n",bad); return bad;
}
