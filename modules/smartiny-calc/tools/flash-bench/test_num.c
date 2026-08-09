#include "num.c"
#include <stdio.h>
#include <string.h>
static num_t mk(const char *digits, int e, int neg){
    num_t n; memset(&n,0,sizeof n); n.exp=(int8_t)e; n.flags=neg?NUM_NEG:0;
    for(uint8_t i=0;i<NDIG && digits[i];i++) dset(&n,i,(uint8_t)(digits[i]-'0'));
    return n;
}
static int fails=0;
static void chk(const char*what,num_t*v,const char*want){
    char b[32]; num_format(v,b);
    if(strcmp(b,want)){ printf("  FAIL %-14s got %-16s want %s\n",what,b,want); fails++; }
    else printf("  ok   %-14s %s\n",what,b);
}
int main(void){
    num_t a,b,r;
    a=mk("1",0,0);        chk("1",&a,"1");
    a=mk("15",1,0);       chk("15",&a,"15");
    a=mk("1",-1,0);       chk("0.1",&a,"0.1");
    a=mk("123456789",5,0);chk("123456.789",&a,"123456.789");
    a=mk("1",6,0);        chk("1e6",&a,"1000000");
    a=mk("1",12,0);       chk("1e12",&a,"1E+12");
    a=mk("31415926",0,0); chk("pi",&a,"3.1415926");
    a=mk("5",0,1);        chk("-5",&a,"-5");
    /* the test every calculator must pass */
    a=mk("1",-1,0); b=mk("2",-1,0); num_add(&r,&a,&b); chk("0.1+0.2",&r,"0.3");
    a=mk("1",1,0);  b=mk("5",0,0);  num_add(&r,&a,&b); chk("10+5",&r,"15");
    a=mk("15",1,0); b=mk("2",0,0);  num_mul(&r,&a,&b); chk("15*2",&r,"30");
    a=mk("15",1,0); b=mk("15",1,0); num_mul(&r,&a,&b); chk("15*15",&r,"225");
    a=mk("1",0,0);  b=mk("3",0,0);  num_div(&r,&a,&b); chk("1/3",&r,"0.3333333333");
    a=mk("1",0,0);  b=mk("8",0,0);  num_div(&r,&a,&b); chk("1/8",&r,"0.125");
    a=mk("3",0,0);  b=mk("5",0,1);  num_add(&r,&a,&b); chk("3+(-5)",&r,"-2");
    printf("%s\n", fails?"FAILURES":"all pass");
    return fails!=0;
}
