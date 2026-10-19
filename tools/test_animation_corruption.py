"""Stress both native animation parsers with deterministic corruptions under sanitizers."""
import json, os, subprocess, sys, tempfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
DRIVER=r'''
#define main launcher_main
#include SOURCE_FILE
#undef main
#include <assert.h>
static uint32_t seed=0x7ac3195u;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void check(uint8_t *data,size_t size) {
 struct image image={0};int rc=parse_animation(&image,data,size);
 assert(rc==0||rc==-1);
 if(!rc){assert(image.storage==data&&image.frame_count>0&&image.frame_count<=FRAMECAP);free(image.frames);}
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>16&&length<=ANIMCAP);rewind(f);
 size_t size=(size_t)length;uint8_t *original=malloc(size),*data=malloc(size+8);assert(original&&data);assert(fread(original,1,size,f)==size);fclose(f);
 struct image valid={0};assert(!parse_animation(&valid,original,size));free(valid.frames);
 for(size_t n=0;n<24;n++){memcpy(data,original,size);check(data,n);}
 for(int i=0;i<12000;i++){
  memcpy(data,original,size);size_t n=size;
  switch(i%4){
   case 0:n=random_word()%(size+1);break;
   case 1:for(int j=0;j<8;j++)data[random_word()%16]=(uint8_t)random_word();break;
   case 2:for(int j=0;j<8;j++)data[random_word()%size]^=(uint8_t)(1u<<(random_word()%8));break;
   default:n=size+1+random_word()%8;memset(data+size,0,n-size);break;
  }
  check(data,n);
 }
 free(data);free(original);puts("PASS: 12024 truncated, header-corrupt, bit-flipped and trailing-byte animations under ASan/UBSan");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='buttongoblin-animation-review-') as directory:
    for device,fixture in zip(['mini','mk2'],sys.argv[1:],strict=True):
        source=root/device/f'buttongoblin-{device}.c'
        driver=Path(directory)/f'{device}.c';binary=driver.with_suffix('')
        driver.write_text(DRIVER.replace('SOURCE_FILE',json.dumps(str(source))))
        subprocess.run(['gcc','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(driver),'-o',str(binary)],check=True)
        subprocess.run([str(binary),str(Path(fixture).resolve())],check=True,env=os.environ.copy())
