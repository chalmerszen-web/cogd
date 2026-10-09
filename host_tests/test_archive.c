#include "context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char flash[262144],saved[sizeof(flash)];
static char scratch[AGENT_REQUEST_MAX+1],output[AGENT_REQUEST_MAX+1];
static agent_context_t context;
static uint64_t reserved;
static long remaining=-1;
static unsigned erases;
static agent_err_t read_flash(void *ctx,size_t offset,void *data,size_t size)
{ (void)ctx; assert(offset+size<=sizeof(flash)); memcpy(data,flash+offset,size); return AGENT_OK; }
static agent_err_t write_flash(void *ctx,size_t offset,const void *data,size_t size)
{
    (void)ctx; const unsigned char *source=data; assert(offset+size<=sizeof(flash));
    for(size_t i=0;i<size;++i) {
        if(!remaining) return AGENT_ERR_STORAGE;
        if(remaining>0) --remaining;
        assert((flash[offset+i]&source[i])==source[i]); flash[offset+i]&=source[i];
    }
    return AGENT_OK;
}
static agent_err_t erase_flash(void *ctx,size_t offset,size_t size)
{
    (void)ctx; assert(!(offset%4096) && !(size%4096) && offset+size<=sizeof(flash)); ++erases;
    for(size_t i=0;i<size;++i) {
        if(!remaining) return AGENT_ERR_STORAGE;
        if(remaining>0) --remaining;
        flash[offset+i]=255;
    }
    return AGENT_OK;
}
static agent_err_t reserve(void *ctx,uint64_t *first,uint64_t *last)
{ (void)ctx; *first=reserved+1; reserved+=128; *last=reserved; return AGENT_OK; }
static const agent_flash_ops_t ops={read_flash,write_flash,erase_flash,NULL,sizeof(flash),4096};
static void boot(void)
{
    memset(&context,0,sizeof(context)); context.device="device-a"; context.user="user"; context.session="session";
    context.reserve=reserve; context.scratch=scratch; context.capacity=sizeof(scratch);
    assert(!agent_context_open(&context,&ops));
}
static void turn(unsigned number)
{
    char content[256];
    snprintf(content,sizeof(content),"{\"messages\":[{\"role\":\"user\",\"content\":\"question-%u\"},{\"role\":\"assistant\",\"content\":\"answer-%u\"}]}",number,number);
    assert(!agent_context_emit(&context,"turn","assistant",content,NULL,0));
}
int main(void)
{
    memset(flash,255,sizeof(flash)); agent_wal_t wal; assert(!agent_wal_format(&wal,&ops)); boot();
    turn(0); uint64_t source=context.last_local;
    assert(agent_context_archive(&context,context.wal.generation,context.wal.next_seq-1)==AGENT_ERR_FORBIDDEN);
    assert(!agent_context_checkpoint(&context,0,0,AGENT_CONTEXT_LOCAL));
    assert(!agent_context_emit(&context,"memory","user","{\"key\":\"color\",\"value\":\"green\"}",NULL,0));
    assert(!agent_context_emit(&context,"memory","user","{\"key\":\"gone\",\"value\":\"old\"}",NULL,0));
    assert(!agent_context_emit(&context,"tombstone","user","{\"key\":\"gone\"}",NULL,0));
    assert(!agent_context_summary_set(&context,"Original preference summary",source));
    for(unsigned i=1;i<=140;++i) turn(i);
    uint64_t through=context.wal.next_seq-1;
    /* Events written after the exported prefix must never be reclaimed. */
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"after-export\"}",NULL,0));
    uint64_t after_export=context.wal.next_seq-1, generation=context.wal.generation;
    size_t before=context.events; unsigned erased=erases;
    assert(agent_context_archive(&context,generation,0)==AGENT_ERR_ARGUMENT);
    assert(agent_context_archive(&context,generation,context.wal.next_seq)==AGENT_ERR_ARGUMENT);
    assert(agent_context_archive(&context,generation+1,through)==AGENT_ERR_BUSY);
    context.prompt_locked=true;
    assert(agent_context_archive(&context,generation,through)==AGENT_ERR_BUSY); context.prompt_locked=false;
    assert(erases==erased && !context.archived_record && !context.acked);
    memcpy(saved,flash,sizeof(flash));
    const long cuts[]={0,4095,131071,131080,132000,140000};
    for(unsigned i=0;i<sizeof(cuts)/sizeof(*cuts);++i) {
        memcpy(flash,saved,sizeof(flash)); boot(); remaining=cuts[i];
        assert(agent_context_archive(&context,generation,through)==AGENT_ERR_STORAGE);
        assert(!context.archived_record && context.wal.generation==generation);
        remaining=-1; boot(); assert(context.events==before && !context.archived_record);
    }
    assert(!agent_context_archive(&context,generation,through));
    assert(context.archived_record==through && context.events==130 && context.recent_count==128);
    assert(!context.acked && !context.cursor && context.pending==context.events);
    assert(!strcmp(context.memory[0].value,"green") && context.memory[1].deleted);
    assert(!strcmp(context.summary.text,"Original preference summary") && context.summary.through==source);
    assert(!agent_context_search(&context,"question-0",0,1,output,sizeof(output)) && strstr(output,"question-0"));
    agent_record_t last=context.recent[127];
    assert(!agent_wal_read(&context.wal,&last,output,sizeof(output)) && strstr(output,"answer-140"));
    erased=erases;
    assert(!agent_context_archive(&context,generation,through) && erases==erased);
    assert(context.wal.next_seq>after_export);
    boot(); assert(context.archived_record==through && context.events==130 && !context.acked);
    assert(!agent_context_compact(&context)); boot(); assert(context.events==130);
    assert(!agent_context_summary_set(&context,"Still has original source",source));
    uint64_t previous=context.last_local;
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"new\"}",NULL,0) && context.last_local>previous);

    /* A completely full LOCAL bank remains recoverable through explicit export,
     * without allocating space for an ACK/checkpoint in the already-full bank. */
    assert(!agent_wal_format(&wal,&ops)); boot();
    assert(!agent_context_checkpoint(&context,0,0,AGENT_CONTEXT_LOCAL));
    turn(7);
    agent_err_t error;
    do { error=agent_context_emit(&context,"message","user","{\"text\":\"pending filler\"}",NULL,0); } while(!error);
    assert(error==AGENT_ERR_FULL); before=context.events; boot(); assert(context.events==before);
    assert(!agent_context_archive(&context,context.wal.generation,context.wal.next_seq-1));
    assert(context.events==1 && context.recent_count==1 && !context.acked);
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"chat resumes\"}",NULL,0));
    boot(); assert(context.events==2 && context.archived_record);
    puts("verified-prefix retention, recent turns, summary source, memory/tombstone, power loss and full-bank recovery passed");
}
