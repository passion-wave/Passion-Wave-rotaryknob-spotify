// SPDX-License-Identifier: MIT
#include "pw_weather_inflate.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef PW_WEATHER_HOST
#include <zlib.h>
#else
#include "miniz.h"
#endif
static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t crc(const uint8_t *p,size_t n) {
    // Small portable CRC avoids differing ROM CRC seed/finalization conventions.
    uint32_t value=0xffffffffu;
    for (size_t i=0;i<n;++i) {
        value^=p[i];
        for(unsigned b=0;b<8;++b) value=(value>>1)^(0xedb88320u & (uint32_t)-(int32_t)(value&1));
    }
    return value^0xffffffffu;
}
static bool inflate_bytes(const uint8_t *input,size_t length,bool zlib_header,uint8_t *output,size_t capacity,size_t *written) {
#ifdef PW_WEATHER_HOST
    z_stream stream={0};
    stream.next_in=(unsigned char *)input; stream.avail_in=(uInt)length;
    stream.next_out=output; stream.avail_out=(uInt)capacity;
    if(inflateInit2(&stream,zlib_header?15:-15)!=Z_OK) return false;
    int result=inflate(&stream,Z_FINISH);
    *written=stream.total_out;
    bool ok=result==Z_STREAM_END && stream.avail_in==0;
    inflateEnd(&stream);
    return ok;
#else
    tinfl_decompressor *d=calloc(1,sizeof(*d));
    if(!d) return false;
    tinfl_init(d);
    size_t in=length,out=capacity;
    unsigned flags=TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF|(zlib_header?TINFL_FLAG_PARSE_ZLIB_HEADER:0);
    tinfl_status status=tinfl_decompress(d,input,&in,output,output,&out,flags);
    free(d);
    *written=out;
    return status==TINFL_STATUS_DONE && in==length;
#endif
}
bool pw_weather_decode_body(const uint8_t *in,size_t n,const char *encoding,uint8_t *out,size_t capacity,size_t *written) {
    if(!in || !out || !written || !encoding) return false;
    *written=0;
    if(!*encoding || !strcasecmp(encoding,"identity")) {
        if(n>capacity) return false;
        memcpy(out,in,n); *written=n; return true;
    }
    if(!strcasecmp(encoding,"deflate")) return inflate_bytes(in,n,true,out,capacity,written);
    if(strcasecmp(encoding,"gzip") || n<18 || in[0]!=0x1f || in[1]!=0x8b || in[2]!=8 || (in[3]&0xe0)) return false;
    uint8_t flags=in[3]; size_t off=10;
    if(flags&4) {
        if(off+2>n-8) return false;
        size_t extra=in[off]|((size_t)in[off+1]<<8); off+=2;
        if(extra>n-8-off) return false;
        off+=extra;
    }
    for(unsigned flag=8;flag<=16;flag<<=1) if(flags&flag) {
        while(off<n-8 && in[off]) ++off;
        if(off==n-8) return false;
        ++off;
    }
    if(flags&2) {
        if(off+2>n-8 || (crc(in,off)&0xffffu)!=(uint32_t)(in[off]|((unsigned)in[off+1]<<8))) return false;
        off+=2;
    }
    if(off>n-8 || little32(in+n-4)>capacity) return false;
    if(!inflate_bytes(in+off,n-8-off,false,out,capacity,written)) return false;
    return *written==little32(in+n-4) && crc(out,*written)==little32(in+n-8);
}
