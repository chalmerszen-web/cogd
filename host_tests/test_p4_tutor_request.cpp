#include "p4_tutor_request.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>
static size_t allocation_limit=SIZE_MAX,largest_request=0;
static void *bounded_alloc(size_t n) {
    if(n>largest_request)largest_request=n;
    return n<=allocation_limit?std::malloc(n):nullptr;
}
int main() {
    // Match the failed physical JPEG's 895,272 bytes after base64 encoding.
    const std::string photo="data:image/jpeg;base64,"+std::string((895272+2)/3*4,'A');
    cJSON_Hooks hooks{bounded_alloc,std::free};cJSON_InitHooks(&hooks);
    auto request=cJSON_CreateObject();auto content=cJSON_AddArrayToObject(request,"content");
    assert(p4_tutor::request_photo(content,photo.c_str()));
    auto url=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content,0),"image_url"),"url");
    assert(url->valuestring==photo.data());
    cJSON_AddStringToObject(request,"text","中文题干：12 - x = 9\n引号\"和反斜线\\必须保留");
    char *expected=cJSON_PrintUnformatted(request);assert(expected);
    allocation_limit=1300000;largest_request=0;
    char *old=cJSON_PrintUnformatted(request);
    assert(!old && largest_request>allocation_limit); // original allocator failure
    largest_request=0;char *body=p4_tutor::print_request(request,photo.size());
    assert(body && largest_request<=allocation_limit && !std::strcmp(body,expected));
    // JSON deletion must not release the borrowed photo; the printed request
    // stays intact for HTTP, even after the tree is gone.
    cJSON_Delete(request);assert(photo.back()=='A');
    auto parsed=cJSON_Parse(body);assert(parsed);
    auto parsed_url=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(parsed,"content"),0),"image_url"),"url");
    assert(!std::strcmp(parsed_url->valuestring,photo.c_str()));
    cJSON_Delete(parsed);cJSON_free(body);cJSON_free(expected);
    assert(!p4_tutor::print_request(nullptr,SIZE_MAX));
    allocation_limit=1;assert(!p4_tutor::print_request(nullptr,photo.size()));
    cJSON_InitHooks(nullptr);
    std::cout<<"PASS exact photo JSON under bounded allocation; original growth fails\n";
}
