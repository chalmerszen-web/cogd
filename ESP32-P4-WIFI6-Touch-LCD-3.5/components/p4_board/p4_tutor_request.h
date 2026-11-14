#pragma once
#include "cJSON.h"
#include <climits>
#include <cstddef>

namespace p4_tutor {
/* The caller retains the photo until the JSON tree has been printed/deleted.
 * Referencing it avoids an extra megabyte-sized base64 copy. */
inline bool request_photo(cJSON *content,const char *photo) {
    if(!photo || !*photo)return true;
    auto image=cJSON_CreateObject();
    if(!image)return false;
    auto type=cJSON_AddStringToObject(image,"type","image_url");
    auto url=cJSON_AddObjectToObject(image,"image_url");
    auto reference=cJSON_CreateStringReference(photo);
    if(!type || !url || !reference) {cJSON_Delete(reference);cJSON_Delete(image);return false;}
    if(!cJSON_AddItemToObject(url,"url",reference)) {cJSON_Delete(reference);cJSON_Delete(image);return false;}
    if(!cJSON_AddItemToArray(content,image)) {cJSON_Delete(image);return false;}
    return true;
}
inline char *print_request(const cJSON *request,size_t photo_bytes) {
    constexpr size_t envelope=32768;
    if(photo_bytes>size_t(INT_MAX)-envelope)return nullptr;
    /* cJSON's default geometric growth asks for nearly twice the large image
     * string at once. Reserve the known image size plus small text overhead;
     * PrintBuffered preserves identical escaping and can grow for long text. */
    return cJSON_PrintBuffered(request,int(photo_bytes+envelope),false);
}
} // namespace p4_tutor
