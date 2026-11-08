#pragma once
#include <cstddef>
inline int mbedtls_base64_encode(unsigned char* out,size_t cap,size_t* written,const unsigned char* input,size_t size){
 static const char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 const size_t need=((size+2)/3)*4;if(cap<=need)return -1;size_t n=0;
 for(size_t i=0;i<size;i+=3){unsigned v=unsigned(input[i])<<16;if(i+1<size)v|=unsigned(input[i+1])<<8;if(i+2<size)v|=input[i+2];out[n++]=chars[v>>18];out[n++]=chars[(v>>12)&63];out[n++]=i+1<size?chars[(v>>6)&63]:'=';out[n++]=i+2<size?chars[v&63]:'=';}
 out[n]=0;*written=n;return 0;
}
