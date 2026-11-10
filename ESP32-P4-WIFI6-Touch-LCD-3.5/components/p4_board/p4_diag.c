#include "p4_diag.h"
#include "p4_diag_format.h"
#include "p4_storage_layout.h"
#include "p4_audio.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

enum {PAGES=P4_DIAG_BYTES/P4_DIAG_PAGE};
static const esp_partition_t *partition;
static size_t base;
static uint8_t *buffers[2];
static unsigned active;
static uint32_t used=P4_DIAG_HEADER,dropped,truncated,boot_id;
static portMUX_TYPE buffer_lock=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t io_lock;
static atomic_bool enabled,paused,ready;
static atomic_uint io_errors,write_max_us;
static uint64_t sequences[PAGES],next_sequence=1,last_uptime,first_uptime;
static unsigned next_slot,valid_pages;
static uint64_t pause_until;
static esp_reset_reason_t reset_reason;

static uint64_t now_ms(void) {return (uint64_t)esp_timer_get_time()/1000;}
static uint64_t unix_ms(void) {
    struct timeval t;gettimeofday(&t,NULL);
    return t.tv_sec>=1735689600 ? (uint64_t)t.tv_sec*1000+(unsigned)t.tv_usec/1000:0;
}
static int sensitive(const char *p,size_t n) {
    static const char *const names[]={"authorization","api_key","api-key","bearer ","sk-","access_token","data:image/","data:audio/"};
    for(unsigned k=0;k<sizeof(names)/sizeof(names[0]);k++) {
        size_t z=strlen(names[k]);
        for(size_t i=0;i+z<=n;i++) {
            size_t j=0;for(;j<z;j++){unsigned char c=(unsigned char)p[i+j];if(c>='A'&&c<='Z')c+=32;if(c!=(unsigned char)names[k][j])break;}
            if(j==z)return 1;
        }
    }
    return 0;
}
void p4_diag_record(unsigned kind,const char *text,size_t size) {
    if(!atomic_load(&ready) || !atomic_load(&enabled) || atomic_load(&paused) || !size || xPortInIsrContext())return;
    uint8_t flags=0;
    if(size>P4_DIAG_TEXT){size=P4_DIAG_TEXT;flags=1;}
    if(sensitive(text,size)){text="[credential or media payload omitted]";size=strlen(text);flags|=2;}
    uint64_t at=now_ms();
    portENTER_CRITICAL(&buffer_lock);
    if(flags&1)++truncated;
    if(!p4_diag_append(buffers[active],&used,at,(uint8_t)kind,flags,text,(uint16_t)size))++dropped;
    portEXIT_CRITICAL(&buffer_lock);
}
static void boot_record(void) {
    const esp_app_desc_t *app=esp_app_get_description();char sha[65],text[224];
    for(unsigned i=0;i<32;i++)snprintf(sha+2*i,3,"%02x",app->app_elf_sha256[i]);
    int n=snprintf(text,sizeof(text),"boot=%08lx reset_reason=%u version=%s elf_sha256=%s",
        (unsigned long)boot_id,(unsigned)reset_reason,app->version,sha);
    if(n>0)p4_diag_record(4,text,(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1);
}
/* The writer owns io_lock. The producer only switches buffers under a short
 * spinlock. No Flash operation or CRC work is done in the producer path. */
static int flush_page(void) {
    unsigned capture[5];p4_audio_capture_stats(capture);
    char text[320];int n=snprintf(text,sizeof(text),"heap=%u internal=%u internal_min=%u largest=%u log_io_errors=%u capture_frames=%u capture_max_us=%u capture_over_32ms=%u rx_overflows=%u",
        (unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),atomic_load(&io_errors),
        capture[0],capture[1],capture[3],capture[4]);
    p4_diag_record(3,text,(size_t)n);
    unsigned staging;uint32_t bytes,drop,trim;
    portENTER_CRITICAL(&buffer_lock);
    staging=active;active^=1;bytes=used;used=P4_DIAG_HEADER;drop=dropped;trim=truncated;
    portEXIT_CRITICAL(&buffer_lock);
    uint8_t *page=buffers[staging];
    /* The active buffer was erased in RAM after its previous write. */
    p4_diag_header_t h={.sequence=next_sequence,.boot=boot_id,.used=bytes,.uptime_ms=now_ms(),
        .unix_ms=unix_ms(),.dropped=drop,.truncated=trim,.reset_reason=(uint32_t)reset_reason};
    p4_diag_seal(page,&h);
    size_t offset=base+(size_t)next_slot*P4_DIAG_PAGE;
    int64_t began=esp_timer_get_time();
    esp_err_t error=esp_partition_erase_range(partition,offset,P4_DIAG_PAGE);
    if(!error)error=esp_partition_write(partition,offset,page,P4_DIAG_END);
    if(!error)error=esp_partition_write(partition,offset+P4_DIAG_END,page+P4_DIAG_END,4);
    unsigned duration=(unsigned)(esp_timer_get_time()-began);
    if(duration>atomic_load(&write_max_us))atomic_store(&write_max_us,duration);
    if(error) {
        atomic_fetch_add(&io_errors,1);
        if(sequences[next_slot]){sequences[next_slot]=0;--valid_pages;}
        /* Never continue erasing after a storage fault; preserve other pages. */
        atomic_store(&paused,true);pause_until=UINT64_MAX;
    } else {
        if(!sequences[next_slot])++valid_pages;
        sequences[next_slot]=next_sequence++;next_slot=(next_slot+1)%PAGES;
        if(!first_uptime)first_uptime=h.uptime_ms;
        last_uptime=h.uptime_ms;
    }
    memset(page,0xff,P4_DIAG_PAGE);
    return error?16:0;
}
static void writer(void *unused) {
    (void)unused;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(P4_DIAG_INTERVAL_MS));
        if(xSemaphoreTake(io_lock,pdMS_TO_TICKS(100))!=pdTRUE)continue;
        if(atomic_load(&paused) && pause_until!=UINT64_MAX && now_ms()>=pause_until)atomic_store(&paused,false);
        if(atomic_load(&enabled) && !atomic_load(&paused))flush_page();
        xSemaphoreGive(io_lock);
    }
}
void p4_diag_init(void) {
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"media");
    if(!partition || partition->address!=0x880000 || partition->size!=0x780000)return;
    base=p4_media_cache_end(partition->size);
    buffers[0]=heap_caps_malloc(P4_DIAG_PAGE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    buffers[1]=heap_caps_malloc(P4_DIAG_PAGE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    io_lock=xSemaphoreCreateMutex();
    if(!buffers[0] || !buffers[1] || !io_lock) {
        free(buffers[0]);free(buffers[1]);buffers[0]=buffers[1]=NULL;
        if(io_lock)vSemaphoreDelete(io_lock);
        return;
    }
    uint64_t highest=0;
    for(unsigned i=0;i<PAGES;i++) {
        p4_diag_header_t h;
        if(esp_partition_read(partition,base+i*P4_DIAG_PAGE,buffers[0],P4_DIAG_PAGE)==ESP_OK && p4_diag_valid(buffers[0],&h)) {
            sequences[i]=h.sequence;++valid_pages;
            if(h.sequence>highest){highest=h.sequence;next_slot=(i+1)%PAGES;}
        }
    }
    if(highest==UINT64_MAX)return;
    next_sequence=highest+1;
    memset(buffers[0],0xff,P4_DIAG_PAGE);memset(buffers[1],0xff,P4_DIAG_PAGE);
    reset_reason=esp_reset_reason();boot_id=esp_random();
    nvs_handle_t nv;uint8_t on=0;
    if(nvs_open("p4_diag",NVS_READONLY,&nv)==ESP_OK){nvs_get_u8(nv,"enabled",&on);nvs_close(nv);}
    atomic_store(&enabled,on!=0);atomic_store(&ready,true);
    boot_record();
    if(xTaskCreate(writer,"p4_log",4096,NULL,1,NULL)!=pdPASS){atomic_store(&ready,false);return;}
}
static int status(char *out,size_t cap) {
    uint64_t oldest=next_sequence;
    for(unsigned i=0;i<PAGES;i++)if(sequences[i] && sequences[i]<oldest)oldest=sequences[i];
    uint32_t drop,trim,buffered;
    portENTER_CRITICAL(&buffer_lock);drop=dropped;trim=truncated;buffered=used-64;portEXIT_CRITICAL(&buffer_lock);
    int n=snprintf(out,cap,"{\"log\":1,\"ready\":true,\"enabled\":%s,\"paused\":%s,\"boot\":%lu,\"reset_reason\":%u,"
        "\"uptime_ms\":%llu,\"unix_ms\":%llu,\"first_sequence\":%llu,\"last_sequence\":%llu,\"pages\":%u,\"page_bytes\":4096,"
        "\"capacity_bytes\":%u,\"interval_ms\":2500,\"window_seconds\":640,\"buffered_bytes\":%u,\"dropped\":%u,\"truncated\":%u,"
        "\"io_errors\":%u,\"write_max_us\":%u,\"current_boot_first_ms\":%llu,\"current_boot_last_ms\":%llu}",
        atomic_load(&enabled)?"true":"false",atomic_load(&paused)?"true":"false",(unsigned long)boot_id,(unsigned)reset_reason,
        (unsigned long long)now_ms(),(unsigned long long)unix_ms(),(unsigned long long)(valid_pages?oldest:0),
        (unsigned long long)(valid_pages?next_sequence-1:0),valid_pages,P4_DIAG_BYTES,(unsigned)buffered,(unsigned)drop,(unsigned)trim,
        atomic_load(&io_errors),atomic_load(&write_max_us),(unsigned long long)first_uptime,(unsigned long long)last_uptime);
    return n<0 || (size_t)n>=cap?5:0;
}
int p4_diag_command(const char *line,char *out,size_t cap) {
    if(strncmp(line,"agent log ",10))return -1;
    if(!atomic_load(&ready)){snprintf(out,cap,"{\"log\":1,\"ready\":false}");return 2;}
    if(xSemaphoreTake(io_lock,pdMS_TO_TICKS(3000))!=pdTRUE)return 3;
    const char *arg=line+10;int error=0;
    if(!strcmp(arg,"on") || !strcmp(arg,"off")) {
        bool on=!strcmp(arg,"on");nvs_handle_t nv;
        esp_err_t e=nvs_open("p4_diag",NVS_READWRITE,&nv);
        if(!e){e=nvs_set_u8(nv,"enabled",on?1:0);if(!e)e=nvs_commit(nv);nvs_close(nv);}
        if(e)error=16;
        else {
            if(!on && atomic_load(&enabled) && !atomic_load(&paused))error=flush_page();
            atomic_store(&enabled,on);atomic_store(&paused,false);pause_until=0;
            if(on)boot_record();
        }
    } else if(!strcmp(arg,"freeze")) {
        if(atomic_load(&enabled) && !atomic_load(&paused))error=flush_page();
        /* Reading an already fault-frozen journal must not restart writes. */
        if(pause_until!=UINT64_MAX)pause_until=now_ms()+600000;
        atomic_store(&paused,true);
    } else if(!strcmp(arg,"resume")) {atomic_store(&paused,false);pause_until=0;}
    else if(!strncmp(arg,"mark ",5))p4_diag_record(5,arg+5,strlen(arg+5));
    else if(!strncmp(arg,"read ",5)) {
        unsigned long long seq;unsigned offset,count;char extra;
        if(strchr(arg,'-') || sscanf(arg+5,"%llu %u %u %c",&seq,&offset,&count,&extra)!=3 ||
           !seq || !count || count>256 || offset>=4096 || count>4096-offset)error=1;
        else {
            unsigned slot=PAGES;for(unsigned i=0;i<PAGES;i++)if(sequences[i]==seq){slot=i;break;}
            if(slot==PAGES)error=23;
            else {
                uint8_t bytes[256];
                if(esp_partition_read(partition,base+slot*4096+offset,bytes,count)!=ESP_OK)error=16;
                else {
                    int n=snprintf(out,cap,"{\"log_page\":%llu,\"offset\":%u,\"hex\":\"",seq,offset);
                    if(n<0 || (size_t)n+count*2+3>=cap)error=5;
                    else {static const char hex[]="0123456789abcdef";for(unsigned i=0;i<count;i++){out[n++]=hex[bytes[i]>>4];out[n++]=hex[bytes[i]&15];}memcpy(out+n,"\"}",3);}
                }
            }
        }
        xSemaphoreGive(io_lock);return error;
    } else if(strcmp(arg,"status"))error=1;
    if(!error)error=status(out,cap);
    xSemaphoreGive(io_lock);return error;
}
