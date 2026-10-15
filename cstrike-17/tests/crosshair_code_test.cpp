/*
crosshair_code_test.cpp -- checks of the crosshair share codes

	g++ -O1 -Icl_dll -o crosshair_code_test tests/crosshair_code_test.cpp cl_dll/crosshair_code.cpp && ./crosshair_code_test

Random crosshairs must come back exactly from their code and through the
cvars, a changed character must never import a different crosshair, and a
code from a newer version must import its known fields.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include "crosshair_code.h"
static std::map<std::string,std::string> cvars;
static const char *get(const char *n){ return cvars.count(n)?cvars[n].c_str():""; }
static void set(const char *n,const char *v){ cvars[n]=v; }
static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
static float rnd(){ return (rand()%20001-10000)/ (float)(1+rand()%997); }
int main(){
  xhair_config_t a,b; char code[XHAIR_CODE_MAX]; xhair_import_t info;
  XHair_DefaultConfig(&a); CHECK(XHair_Encode(&a,code,sizeof code)); printf("default: %s\n",code);
  for(int i=0;i<XHair_NumBuiltinPresets();i++){ const char*n=XHair_BuiltinPreset(i,&a); XHair_Encode(&a,code,sizeof code);
    CHECK(XHair_Decode(code,&b,&info)==XHAIR_CODE_OK && XHair_SameConfig(&a,&b)); printf("%-20s %s\n",n,code);}
  // random configs, through the cvars too
  size_t longest=0;
  for(int k=0;k<20000;k++){
    XHair_DefaultConfig(&a);
    a.enable=rand()&1; a.style=rand()%2; a.t=rand()&1; a.dot=rand()&1; a.additive=rand()&1; a.aa=rand()&1; a.dynamic_move=rand()&1; a.weapon_gap=rand()&1;
    float *f[]={&a.gap,&a.size,&a.thick,&a.dot_size,&a.pad,&a.scale,&a.dynamic_scale};
    for(auto p:f) *p = (rand()%3==0)? rnd() : (rand()%2? (rand()%4000-2000)/100.0f : *p);
    for(int i=0;i<4;i++){ a.color[i]=rand()%256; a.pad_color[i]=rand()%256; }
    a.pad_alpha=rand()&1; if(!a.pad_alpha) a.pad_color[3]=255;
    CHECK(XHair_Encode(&a,code,sizeof code)); longest=std::max(longest,strlen(code));
    CHECK(XHair_Decode(code,&b,&info)==XHAIR_CODE_OK); CHECK(XHair_SameConfig(&a,&b)); CHECK(info.unknown==0);
    cvars.clear(); XHair_WriteConfig(&b,set); XHair_ReadConfig(&b,get); CHECK(XHair_SameConfig(&a,&b));
    // lower case, no dashes
    std::string s; for(char*c=code;*c;c++) if(*c!='-') s+=(char)tolower(*c); s.insert(2,"-");
    CHECK(XHair_Decode(s.c_str(),&b,NULL)==XHAIR_CODE_OK && XHair_SameConfig(&a,&b));
    // one character changed
    std::string t=code; size_t pos=3+rand()%(t.size()-3); if(t[pos]!='-'){ t[pos]= t[pos]=='A'?'B':'A';
      int e=XHair_Decode(t.c_str(),&b,NULL); CHECK(e!=XHAIR_CODE_OK || XHair_SameConfig(&a,&b)); }
    if(fails>5) break;
  }
  printf("longest code %zu chars\n",longest);
  CHECK(XHair_Decode("hello",&b,NULL)==XHAIR_CODE_BAD_TEXT);
  CHECK(XHair_Decode("XH-",&b,NULL)==XHAIR_CODE_BAD_TEXT);
  // a version 2 code with an unknown varint field (id 40) and an unknown bytes field (id 41)
  {
    unsigned char d[64]; int n=0; d[n++]=2; d[n++]=(5<<2)|0; d[n++]=((7000*2) & 0x7f) | 0x80; d[n++]=(7000*2)>>7; // size 7
    d[n++]=((40<<2)|0)&0x7f|0x80; d[n++]=((40<<2)|0)>>7; d[n++]=1; d[n++]=((41<<2)|2)&0x7f|0x80; d[n++]=((41<<2)|2)>>7; d[n++]=2; d[n++]=9; d[n++]=9; d[n++]=(3<<2)|0; d[n++]=2; // t=1
    unsigned crc=0xffff; for(int i=0;i<n;i++){ crc^=d[i]<<8; for(int b2=0;b2<8;b2++) crc=(crc&0x8000)?(crc<<1)^0x1021:crc<<1; crc&=0xffff; }
    d[n++]=crc>>8; d[n++]=crc&0xff;
    const char*b32="0123456789ABCDEFGHJKMNPQRSTVWXYZ"; std::string s="XH-"; unsigned acc=0; int bits=0;
    for(int i=0;i<n;i++){ acc=acc<<8|d[i]; bits+=8; while(bits>=5){ s+=b32[(acc>>(bits-5))&31]; bits-=5; } } if(bits) s+=b32[(acc<<(5-bits))&31];
    int e=XHair_Decode(s.c_str(),&b,&info);
    printf("v2 code %s -> %s, version %d unknown %d partial %d size %g t %d\n",s.c_str(),XHair_CodeError(e),info.version,info.unknown,info.partial,b.size,b.t);
    CHECK(e==0 && info.unknown==2 && b.size==7.0f && b.t && !info.partial);
  }
  // presets file
  xhair_preset_t p[3]={{"Mine","XH-ABC"},{"with \"quote\"","XH-DEF"},{"",""}}; char buf[1024];
  size_t len=XHair_FormatPresets(p,2,buf,sizeof buf); xhair_preset_t q[8]; int c=XHair_ParsePresets(buf,len,q,8);
  printf("%s",buf); CHECK(c==2 && !strcmp(q[0].name,"Mine") && !strcmp(q[1].code,"XH-DEF"));
  printf(fails?"FAILED %d\n":"all ok\n",fails); return fails!=0;
}
