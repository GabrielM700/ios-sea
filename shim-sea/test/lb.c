#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
void con_hex(const char*a,uint64_t b){printf("  %-9s %#llx\n",a,(unsigned long long)b);}
void con_str(const char*a,const char*b){printf("  %-9s %s\n",a,b);}
static uint64_t g_lo,g_hi;
int fdt_chosen_u64(uintptr_t dtb,const char*n,uint64_t*o){ *o = strstr(n,"start")? g_lo:g_hi; return 1;}
static uint64_t j_entry,j_ba;
static void host_jump(uint64_t e,uint64_t b){ j_entry=e; j_ba=b; }
#define FB_BASE_CHECK 0x7c606000UL
#include "loader_host.c"
static uint64_t rd64(const uint8_t*p,int o){uint64_t v;memcpy(&v,p+o,8);return v;}
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  uint8_t*pl=malloc(n); fread(pl,1,n,f); fclose(f);
  uint64_t phys=rd64(pl,0x38), msz=rd64(pl,0x40), virt=rd64(pl,0x10), entry=rd64(pl,0x18);
  uint64_t adt_off=rd64(pl,0x20), adt_sz=rd64(pl,0x28), kend=rd64(pl,0x30);
  uint32_t nsegs; memcpy(&nsegs,pl+12,4);
  void*ram=mmap((void*)phys,0x10000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE|MAP_NORESERVE,-1,0);
  if(ram==MAP_FAILED){perror("mmap");return 2;}
  int bad=0;
  for(int off=0;off<8;off++){
    size_t R=3*1024*1024+off; uint8_t*rd=aligned_alloc(4096,(R+n+8192)); srand(off); for(size_t i=0;i<R+n+8192;i++)rd[i]=rand();
    memset((void*)phys,0,0x4000000);
    memcpy(rd+R,pl,n);
    g_lo=(uint64_t)rd; g_hi=(uint64_t)rd+R+n;
    j_entry=0;
    printf("-- misalign %d\n",off);
    const char*e=loader_boot(0);
    uint64_t d=phys-virt;
    if(!j_entry||j_entry!=entry+d){printf("BAD entry\n");bad++;continue;}
    /* segment check */
    for(uint32_t i=0;i<nsegs;i++){uint64_t va=rd64(pl,0x2a8+32*i),vs=rd64(pl,0x2a8+32*i+8),fo=rd64(pl,0x2a8+32*i+16),fs=rd64(pl,0x2a8+32*i+24);
      if(memcmp((void*)(va+d),pl+fo,fs)){printf("BAD seg %u\n",i);bad++;}
      for(uint64_t k=fs;k<vs;k++) if(((uint8_t*)(va+d))[k]){printf("BAD bss %u\n",i);bad++;break;}}
    uint64_t adt_pa=kend+d;
    if(memcmp((void*)adt_pa,pl+adt_off,adt_sz)){printf("BAD adt\n");bad++;}
    const uint8_t*ba=(void*)j_ba;
    uint16_t rev=*(uint16_t*)ba, ver=*(uint16_t*)(ba+2);
    if(rev!=2||ver!=2||rd64(ba,8)!=virt||rd64(ba,16)!=phys||rd64(ba,24)!=msz){printf("BAD ba hdr\n");bad++;}
    if(rd64(ba,32)!=((adt_pa+adt_sz+0x3fff)&~0x3fffUL)+0x4000){printf("BAD top %llx\n",(unsigned long long)rd64(ba,32));bad++;}
    if(rd64(ba,40)!=FB_BASE_CHECK||rd64(ba,56)!=1080*4||rd64(ba,64)!=1080||rd64(ba,72)!=2400||rd64(ba,80)!=32){printf("BAD video\n");bad++;}
    if(rd64(ba,96)!=adt_pa-d){printf("BAD dtp\n");bad++;}
    if(strcmp((char*)ba+108,(char*)pl+0x48)){printf("BAD cmdline '%s'\n",ba+108);bad++;}
    free(rd);
  }
  printf("bad=%d\n",bad);return bad;
}
