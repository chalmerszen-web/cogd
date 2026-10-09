#include "text_pipe.h"
#include "json.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static void empty(const agent_text_pipe_t *p,size_t offset,bool expected_done)
{
    const char *span=(const char *)1;size_t n=99;bool done=!expected_done;
    assert(!agent_text_pipe_next(p,offset,&span,&n,&done));
    assert(!span && !n && done==expected_done);
}
static size_t drain(const agent_text_pipe_t *p,char *out,size_t offset)
{
    bool done=false;
    while(!done) {
        const char *span;size_t n;
        assert(!agent_text_pipe_next(p,offset,&span,&n,&done));
        assert(n<=AGENT_TEXT_PIPE_SPAN && (!n || agent_utf8_valid(span,n)));
        if(n){memcpy(out+offset,span,n);offset+=n;}
        else assert(done);
    }
    return offset;
}
static void contract(void)
{
    agent_text_pipe_t pipe;
    atomic_bool cancel=false;
    unsigned char storage[AGENT_TEXT_PIPE_CAPACITY+2];memset(storage,0xa5,sizeof(storage));
    char answer[AGENT_TEXT_PIPE_CAPACITY];
    assert(agent_text_pipe_begin(NULL,(char *)storage,1,NULL)==AGENT_ERR_ARGUMENT);
    assert(!agent_text_pipe_begin(&pipe,(char *)storage+1,sizeof(storage)-2,&cancel));
    empty(&pipe,0,false);
    assert(!agent_text_pipe_write(&pipe,"你好，小言。",strlen("你好，小言。")));
    assert(agent_text_pipe_write(&pipe,"\xe4",1)==AGENT_ERR_PROTOCOL);
    assert(agent_text_pipe_write(&pipe,"A\0B",3)==AGENT_ERR_PROTOCOL);
    assert(agent_text_pipe_write(&pipe,"x",AGENT_TEXT_PIPE_CAPACITY+1)==AGENT_ERR_LIMIT);
    agent_text_pipe_finish(&pipe);agent_text_pipe_finish(&pipe);
    size_t n=drain(&pipe,answer,0);
    assert(n==strlen("你好，小言。") && !memcmp(answer,"你好，小言。",n));
    assert(agent_text_pipe_write(&pipe,"after close",11)==AGENT_ERR_PROTOCOL);
    assert(storage[0]==0xa5 && storage[sizeof(storage)-1]==0xa5);

    assert(!agent_text_pipe_begin(&pipe,(char *)storage+1,sizeof(storage)-2,&cancel));
    char full[AGENT_TEXT_PIPE_CAPACITY];
    for(size_t i=0;i<sizeof(full);i+=6)memcpy(full+i,"你好",6);
    assert(!agent_text_pipe_write(&pipe,full,sizeof(full)));
    assert(agent_text_pipe_write(&pipe,"overflow",8)==AGENT_ERR_LIMIT);
    agent_text_pipe_finish(&pipe);
    assert(drain(&pipe,answer,0)==sizeof(full) && !memcmp(answer,full,sizeof(full)));
    assert(storage[0]==0xa5 && storage[sizeof(storage)-1]==0xa5);

    /* Cancellation closes observation even with a readable prefix. Reset only
     * after the producer and consumer have stopped, then verify a clean turn. */
    assert(!agent_text_pipe_begin(&pipe,(char *)storage+1,sizeof(storage)-2,&cancel));
    assert(!agent_text_pipe_write(&pipe,"unplayed",8));atomic_store(&cancel,true);
    const char *span;bool done;size_t length;
    assert(agent_text_pipe_next(&pipe,0,&span,&length,&done)==AGENT_ERR_CANCELLED && !span && !length && !done);
    assert(agent_text_pipe_write(&pipe,"late",4)==AGENT_ERR_CANCELLED);
    agent_text_pipe_finish(&pipe);
    assert(agent_text_pipe_next(&pipe,0,&span,&length,&done)==AGENT_ERR_CANCELLED);
    atomic_store(&cancel,false);
    assert(!agent_text_pipe_begin(&pipe,(char *)storage+1,7,&cancel));
    empty(&pipe,0,false);assert(!agent_text_pipe_write(&pipe,"reset",5));agent_text_pipe_finish(&pipe);
    assert(drain(&pipe,answer,0)==5 && !memcmp(answer,"reset",5));
    assert(!agent_text_pipe_begin(&pipe,(char *)storage+1,7,&cancel));
    agent_text_pipe_finish(&pipe);empty(&pipe,0,true);
}

enum { TURNS=20000 };
static agent_text_pipe_t concurrent;
static char memory[AGENT_TEXT_PIPE_CAPACITY];
static atomic_uint published_turn,consumed_turn;
static const char prefix[]="ASR:你好，小言。广东话同普通话。";
static const char last[]="最后一个字：言";

static void *producer(void *unused)
{
    (void)unused;
    for(unsigned turn=1;turn<=TURNS;++turn) {
        while(atomic_load(&consumed_turn)!=turn-1)sched_yield();
        assert(!agent_text_pipe_begin(&concurrent,memory,sizeof(memory),NULL));
        atomic_store(&published_turn,turn);
        for(unsigned i=0;i<4;++i) {
            assert(!agent_text_pipe_write(&concurrent,prefix,sizeof(prefix)-1));
            if((turn+i)%3==0)sched_yield();
        }
        /* Final append and EOF race against the consumer's empty-prefix read.
         * A stale length followed by a fresh EOF must never lose this suffix. */
        assert(!agent_text_pipe_write(&concurrent,last,sizeof(last)-1));
        agent_text_pipe_finish(&concurrent);
    }
    return NULL;
}
static void *consumer(void *unused)
{
    (void)unused;
    char expected[AGENT_TEXT_PIPE_CAPACITY],received[AGENT_TEXT_PIPE_CAPACITY];
    size_t size=0;
    for(unsigned i=0;i<4;++i){memcpy(expected+size,prefix,sizeof(prefix)-1);size+=sizeof(prefix)-1;}
    memcpy(expected+size,last,sizeof(last)-1);size+=sizeof(last)-1;
    for(unsigned turn=1;turn<=TURNS;++turn) {
        while(atomic_load(&published_turn)!=turn)sched_yield();
        size_t offset=0;bool done=false;
        while(!done) {
            const char *span;size_t n;
            assert(!agent_text_pipe_next(&concurrent,offset,&span,&n,&done));
            assert(n<=AGENT_TEXT_PIPE_SPAN && offset+n<=sizeof(received));
            if(n){assert(agent_utf8_valid(span,n));memcpy(received+offset,span,n);offset+=n;}
            else if(!done)sched_yield();
        }
        assert(offset==size && !memcmp(received,expected,size));
        atomic_store(&consumed_turn,turn);
    }
    return NULL;
}
int main(void)
{
    contract();pthread_t a,b;
    assert(!pthread_create(&a,NULL,producer,NULL));assert(!pthread_create(&b,NULL,consumer,NULL));
    assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));
    puts("text pipe: 20000 concurrent turns retain final UTF-8 suffix, bounded immutable spans, atomic rejection, cancellation, EOF and reset PASS");
}
