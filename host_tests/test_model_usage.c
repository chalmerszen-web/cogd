#include "model_usage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char receipt[4096];static unsigned calls;
void esp_agent_model_receipt(const char *json){assert(strlen(json)<sizeof(receipt));strcpy(receipt,json);++calls;}
static cJSON *run(const char *json,bool terminal){cJSON *r=cJSON_Parse(json);assert(r);agent_model_usage("test-model",r,terminal);cJSON_Delete(r);return cJSON_Parse(receipt);}
int main(void){
 cJSON *r=run("{\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":2,\"total_tokens\":12},\"id\":\"completion-1\"}",true);
 assert(calls==1 && cJSON_GetObjectItem(cJSON_GetObjectItem(r,"usage"),"total_tokens")->valueint==12);cJSON_Delete(r);
 r=run("{\"response\":{\"id\":\"response-1\",\"usage\":{\"input_tokens\":8,\"output_tokens\":3,\"output_token_details\":{\"audio_tokens\":2}}},\"type\":\"response.done\"}",true);
 assert(calls==2 && !strcmp(cJSON_GetObjectItem(r,"path")->valuestring,"response.usage"));cJSON_Delete(r);
 r=run("{\"header\":{\"task_id\":\"tts-1\",\"event\":\"task-finished\"},\"payload\":{\"usage\":{\"characters\":9}}}",true);
 assert(calls==3 && cJSON_GetObjectItem(cJSON_GetObjectItem(r,"usage"),"characters")->valueint==9);cJSON_Delete(r);
 r=run("{\"type\":\"session.finished\"}",true);assert(calls==4 && !cJSON_GetObjectItem(r,"usage") && !strcmp(cJSON_GetObjectItem(r,"availability")->valuestring,"not_returned"));cJSON_Delete(r);
 r=run("{\"type\":\"response.audio.delta\",\"delta\":\"SECRET_AUDIO\"}",false);assert(calls==4 && !strstr(receipt,"SECRET_AUDIO"));cJSON_Delete(r);
 r=run("{\"output\":{\"task_id\":\"video-1\"},\"usage\":{\"duration\":5,\"SR\":480,\"fps\":30}}",true);
 assert(calls==5 && cJSON_GetObjectItem(cJSON_GetObjectItem(r,"usage"),"SR")->valueint==480);cJSON_Delete(r);
 r=run("{\"usage\":null,\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}",false);
 assert(calls==5);cJSON_Delete(r);
 r=run("{\"usage\":null,\"choices\":[]}",true);
 assert(calls==6 && !strcmp(cJSON_GetObjectItem(r,"availability")->valuestring,"not_returned"));cJSON_Delete(r);
 puts("model receipt variants: PASS (tokens, nested audio, characters, missing/null usage, non-usage event, video)");return 0;
}
