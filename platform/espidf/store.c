#include "store.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "control_board.h"

static atomic_uint max_read_ms,max_write_ms,max_erase_ms,max_reserve_ms;
static void elapsed(atomic_uint *maximum,int64_t start)
{
    unsigned ms=(unsigned)((esp_timer_get_time()-start)/1000);
    if(ms>atomic_load(maximum)) atomic_store(maximum,ms);
}
void esp_agent_store_metrics(unsigned out[4])
{
    out[0]=atomic_load(&max_read_ms); out[1]=atomic_load(&max_write_ms);
    out[2]=atomic_load(&max_erase_ms); out[3]=atomic_load(&max_reserve_ms);
}

static agent_err_t rd(void *ctx,size_t off,void *dst,size_t n)
{ int64_t start=esp_timer_get_time(); esp_err_t e=esp_partition_read(ctx,off,dst,n); elapsed(&max_read_ms,start); return e==ESP_OK?AGENT_OK:AGENT_ERR_STORAGE; }
static agent_err_t wr(void *ctx,size_t off,const void *src,size_t n)
{ int64_t start=esp_timer_get_time(); esp_err_t e=esp_partition_write(ctx,off,src,n); elapsed(&max_write_ms,start); return e==ESP_OK?AGENT_OK:AGENT_ERR_STORAGE; }
static agent_err_t er(void *ctx,size_t off,size_t n)
{
    agent_err_t error=esp_hi_storage_begin(); if(error) return error;
    int64_t start=esp_timer_get_time(); esp_err_t e=esp_partition_erase_range(ctx,off,n);
    elapsed(&max_erase_ms,start); esp_hi_storage_end();
    return e==ESP_OK?AGENT_OK:AGENT_ERR_STORAGE;
}
static agent_err_t reserve(void *ctx,uint64_t *first,uint64_t *last)
{
    nvs_handle_t handle=(nvs_handle_t)(uintptr_t)ctx; uint64_t high=0; int64_t start=esp_timer_get_time();
    esp_err_t e=nvs_get_u64(handle,"seq_hwm",&high);
    if(e!=ESP_OK && e!=ESP_ERR_NVS_NOT_FOUND) return AGENT_ERR_STORAGE;
    if(high>AGENT_SEQ_MAX-128) return AGENT_ERR_FULL;
    if(nvs_set_u64(handle,"seq_hwm",high+128)!=ESP_OK || nvs_commit(handle)!=ESP_OK) return AGENT_ERR_STORAGE;
    elapsed(&max_reserve_ms,start); *first=high+1; *last=high+128; return AGENT_OK;
}
agent_err_t esp_agent_store_open(agent_context_t *context,nvs_handle_t handle,bool initialize)
{
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"ctx");
    if(!partition || (partition->size!=1024*1024 && partition->size!=2*1024*1024)) return AGENT_ERR_CONFIG;
    agent_flash_ops_t flash={rd,wr,er,(void *)partition,partition->size,4096};
    context->reserve=reserve; context->reserve_ctx=(void *)(uintptr_t)handle;
    if(initialize) {
        uint8_t initialized=0;
        esp_err_t e=nvs_get_u8(handle,"ctx_init",&initialized);
        if(e!=ESP_OK && e!=ESP_ERR_NVS_NOT_FOUND) return AGENT_ERR_STORAGE;
        if(initialized || context->wal.ready) return AGENT_ERR_FORBIDDEN;
        agent_err_t error=agent_wal_format(&context->wal,&flash); if(error) return error;
        if(nvs_set_u8(handle,"ctx_init",1)!=ESP_OK || nvs_commit(handle)!=ESP_OK) return AGENT_ERR_STORAGE;
    }
    agent_err_t error=agent_context_open(context,&flash);
    if(error) context->wal.ready=false;
    return error;
}
