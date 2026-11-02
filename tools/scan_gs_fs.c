/* Linear-sweep scan of a linked image for gs:/fs: segment-prefixed operands (M4 task 9).
 * usage: scan image.bin base:size,base:size ... start:size ...   (decimal or 0x numbers) */
#include <Zydis/Zydis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  unsigned char*img=malloc(n); if(fread(img,1,n,f)!=(size_t)n) return 1; fclose(f);
  uint64_t mods[8][2]; int nm=0;
  for(char*t=strtok(argv[2],",");t;t=strtok(NULL,",")){ mods[nm][0]=strtoull(t,0,0); char*c=strchr(t,':'); mods[nm][1]=strtoull(c+1,0,0); ++nm; }
  ZydisDecoder d; ZydisDecoderInit(&d,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
  uint64_t patched[9]={0}, other[9]={0}; int printed=0;
  for(int a=3;a<argc;++a){
    uint64_t st=strtoull(argv[a],0,0); char*c=strchr(argv[a],':'); uint64_t sz=strtoull(c+1,0,0);
    for(uint64_t off=0; off<sz;){
      ZydisDecodedInstruction in; ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
      if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&d,img+st+off,sz-off<15?sz-off:15,&in,ops))){ ++off; continue; }
      int seg=-1;
      for(int i=0;i<in.operand_count_visible;++i) if(ops[i].type==ZYDIS_OPERAND_TYPE_MEMORY && (ops[i].mem.segment==ZYDIS_REGISTER_GS||ops[i].mem.segment==ZYDIS_REGISTER_FS)) seg=i;
      if(seg>=0){
        uint64_t at=st+off; int m=nm; for(int k=0;k<nm;++k) if(at>=mods[k][0]&&at-mods[k][0]<mods[k][1]) m=k;
        static const unsigned char pat[9]={0x65,0x48,0x8b,0x04,0x25,0,0,0,0};
        if(in.length==9 && !memcmp(img+at,pat,9)) ++patched[m];
        else { ++other[m]; if(printed<200000){ char buf[128]; ZydisFormatter fm; ZydisFormatterInit(&fm,ZYDIS_FORMATTER_STYLE_INTEL); ZydisFormatterFormatInstruction(&fm,&in,ops,in.operand_count_visible,buf,sizeof buf,at,NULL); printf("  other: image+%#llx module %d: %s  [",(unsigned long long)at,m,buf); for(int b=0;b<in.length;++b)printf("%02x",img[at+b]); printf("]\n"); ++printed; } }
      }
      off+=in.length;
    }
  }
  for(int k=0;k<=nm;++k) printf("%s: patched-form `mov rax, gs:[0]` loads=%llu, other gs:/fs: accesses=%llu\n", k==nm?"eboot (main)":(k==0?"libc.prx":"libSceFios2.prx"), (unsigned long long)patched[k], (unsigned long long)other[k]);
  return 0;
}
