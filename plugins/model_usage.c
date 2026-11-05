#include "model_usage.h"
#include <string.h>

static const cJSON *get(const cJSON *o,const char *k) {return cJSON_GetObjectItemCaseSensitive(o,k);}
static const cJSON *usage_at(const cJSON *o) {
    const cJSON *u=get(o,"usage");return cJSON_IsNull(u)?NULL:u;
}
static void string(cJSON *dst,const char *key,const cJSON *src,const char *from) {
    const cJSON *v=get(src,from);
    if(cJSON_IsString(v) && strlen(v->valuestring)<=160)cJSON_AddStringToObject(dst,key,v->valuestring);
}
void agent_model_usage(const char *model,const cJSON *root,bool terminal) {
    if(!root)return;
    const cJSON *payload=get(root,"payload"),*response=get(root,"response"),*header=get(root,"header");
    const cJSON *usage=usage_at(root); const char *path="usage";
    if(!usage){usage=usage_at(payload);path="payload.usage";}
    if(!usage){usage=usage_at(response);path="response.usage";}
    if(!usage){usage=usage_at(get(root,"output"));path="output.usage";}
    if(!usage && !terminal)return;
    cJSON *out=cJSON_CreateObject();if(!out)return;
    cJSON_AddNumberToObject(out,"schema",1);
    cJSON_AddStringToObject(out,"model",model?model:"provider_unspecified");
    cJSON_AddStringToObject(out,"availability",usage?"returned":"not_returned");
    cJSON_AddBoolToObject(out,"terminal",terminal);
    cJSON_AddStringToObject(out,"aggregation","raw_provider_event_do_not_sum");
    string(out,"request_id",root,"request_id");string(out,"event_id",root,"event_id");
    string(out,"event",root,"type");string(out,"task_id",header,"task_id");
    if(!get(out,"task_id"))string(out,"task_id",get(root,"output"),"task_id");
    string(out,"response_id",response,"id");
    if(!get(out,"event"))string(out,"event",header,"event");
    if(!get(out,"response_id"))string(out,"response_id",root,"id");
    if(usage){cJSON_AddStringToObject(out,"path",path);cJSON_AddItemToObject(out,"usage",cJSON_Duplicate(usage,true));}
    char *text=cJSON_PrintUnformatted(out);
    if(text){esp_agent_model_receipt(text);cJSON_free(text);}
    cJSON_Delete(out);
}
