#pragma once
#include "cJSON.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace aoq {
// Temporary native interoperability telemetry. Print structure only: no string
// values, IDs, payload bytes or arbitrary numbers. Fixed small bounds apply to
// malicious/unknown server structures as well as the expected model schema.
inline void metadata_nodes(const cJSON* node,unsigned depth,unsigned& budget) {
    for(const auto* p=node;p&&budget;p=p->next){
        --budget;
        char name[129]="?";
        if(p->string){
            const size_t n=std::strlen(p->string);
            if(n&&n<sizeof(name)){
                bool safe=true;
                for(size_t i=0;i<n;++i){const char c=p->string[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))safe=false;}
                if(safe)std::memcpy(name,p->string,n+1);
            }
        }
        std::printf(" %u:%s:%d",depth,name,p->type&255);
        if(p->child&&depth<5)metadata_nodes(p->child,depth+1,budget);
        else if(cJSON_IsString(p)&&p->valuestring&&p->valuestring[0]=='{'&&depth<5&&std::strlen(p->valuestring)<=2048){
            auto* nested=cJSON_Parse(p->valuestring);if(nested)metadata_nodes(nested,depth+1,budget);cJSON_Delete(nested);
        }
    }
}
inline void metadata_json(const char* source,const uint8_t* bytes,size_t size){
    if(!bytes||!size||size>16384)return;
    auto* json=cJSON_ParseWithLength(reinterpret_cast<const char*>(bytes),size);
    std::printf("AOQ_SCHEMA %s bytes=%u",source,static_cast<unsigned>(size));
    unsigned budget=64;if(json)metadata_nodes(json,0,budget);
    std::printf("\n");cJSON_Delete(json);
}
}
