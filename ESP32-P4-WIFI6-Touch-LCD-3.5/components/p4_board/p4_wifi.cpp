#include "p4_agent.h"
#include "p4_media.h"
#include "bsp/esp-bsp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
struct Profile {char ssid[33],password[65];};
struct Saved {uint32_t version=1,count=0;Profile p[8]{};} saved;
Profile pending{};
wifi_ap_record_t aps[24]{};
unsigned ap_count=0;
std::atomic<bool> scan_done{false},got_ip{false},disconnected{false},visible{false};
std::atomic<bool> ignore_leave{false};
std::atomic<int> reason{0};
std::atomic<bool> scanning{false};
bool initialized=false,joining=false,manual=false,paused_before=false,persisted=false,network_online=false;
int64_t offline_since=0,next_scan=0,join_deadline=0,cooldown[8]{};
int candidate=-1;
lv_obj_t *panel=nullptr,*info=nullptr,*entry=nullptr;
// All editor state and requests are protected by the existing LVGL mutex.
enum Request {NONE,SCAN,CONNECT};
std::atomic<Request> request{NONE};
enum Page {AP_LIST,EDITOR,CONNECTING};
std::atomic<Page> page{AP_LIST};
Profile edit{},queued{};
bool entering_ssid=false,show_password=false;
int mode=0,last_key=-1;
unsigned cycle=0;
int64_t last_key_at=0;
std::string value;
const char *letters[3][10]={
 {"1","abc","def","ghi","jkl","mno","pqrs","tuv","wxyz","0"},
 {"1","ABC","DEF","GHI","JKL","MNO","PQRS","TUV","WXYZ","0"},
 {"1","2","3","4","5","6","7","8","9","0"}};
const char *symbols[10]={".,?!","@_","+-=","#$%","&*","()/",":;","'\"","[]{}","\\|~^`<>"};
int64_t now(){return esp_timer_get_time()/1000;}
void label(lv_obj_t *parent,const char *text,int x,int y,int w=300) {
    auto l=lv_label_create(parent);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);lv_label_set_text(l,text);
}
void clear_panel(){lv_obj_clean(panel);info=nullptr;entry=nullptr;}
void ap_screen();void keyboard();
void refresh_entry(){
    if(entry){std::string shown=(entering_ssid || show_password)?value:std::string(value.size(),'*');
        lv_label_set_text(entry,shown.empty()?"_":shown.c_str());}
}
void key(int code) {
    if(code<10) {
        const char *set=mode<3?letters[mode][code]:symbols[code];
        bool repeat=mode!=2 && last_key==code && now()-last_key_at<800 && !value.empty();
        if(repeat){cycle=(cycle+1)%strlen(set);value.back()=set[cycle];}
        else if(value.size()<(entering_ssid?32u:64u)){cycle=0;value+=set[0];}
        last_key=code;last_key_at=now();refresh_entry();return;
    }
    last_key=-1;
    if(code==10){if(!value.empty())value.pop_back();refresh_entry();}
    else if(code==11){mode=(mode+1)%4;keyboard();}
    else if(code==12){if(value.size()<(entering_ssid?32u:64u))value+=' ';refresh_entry();}
    else if(code==13){value.clear();refresh_entry();}
    else if(code==14){show_password=!show_password;keyboard();}
    else if(code==15){memset(&edit,0,sizeof(edit));value.clear();ap_screen();}
    else if(code==16) {
        if(entering_ssid){if(value.empty())return;snprintf(edit.ssid,sizeof(edit.ssid),"%s",value.c_str());
            entering_ssid=false;value.clear();keyboard();return;}
        bool hex=value.size()==64 && value.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos;
        if(!value.empty() && (value.size()<8 || (value.size()>63 && !hex))) {
            lv_label_set_text(info,"密码需 8–63 字符，或 64 位十六进制");return;
        }
        snprintf(edit.password,sizeof(edit.password),"%s",value.c_str());queued=edit;request=CONNECT;
        page=CONNECTING;clear_panel();
        label(panel,"正在连接，请稍候…",10,30);label(panel,"连接成功后自动保存",10,65);
    }
}
void clicked(lv_event_t *e){key(int(intptr_t(lv_event_get_user_data(e))));}
lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,intptr_t data) {
    auto b=lv_button_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,reinterpret_cast<void *>(data));
    auto l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);return b;
}
void keyboard() {
    page=EDITOR;
    clear_panel();label(panel,entering_ssid?"输入隐藏网络名称":"输入 Wi-Fi 密码",8,8);
    label(panel,edit.ssid,8,36);entry=lv_label_create(panel);lv_obj_set_pos(entry,8,66);
    lv_obj_set_width(entry,302);lv_label_set_long_mode(entry,LV_LABEL_LONG_SCROLL_CIRCULAR);refresh_entry();
    info=lv_label_create(panel);lv_obj_set_pos(info,8,99);lv_obj_set_width(info,304);
    lv_label_set_text(info,"连续点按切换字母，停顿后输入下一字");
    for(int i=0;i<9;i++)button(panel,mode<3?letters[mode][i]:symbols[i],8+(i%3)*78,145+(i/3)*58,70,50,clicked,i);
    button(panel,"删除",244,145,68,50,clicked,10);button(panel,"重输",244,203,68,50,clicked,13);
    button(panel,show_password?"隐藏":"显示",244,261,68,50,clicked,14);
    button(panel,mode==0?"abc":mode==1?"ABC":mode==2?"123":"符号",8,319,70,50,clicked,11);
    button(panel,mode<3?letters[mode][9]:symbols[9],86,319,70,50,clicked,9);
    button(panel,"空格",164,319,70,50,clicked,12);button(panel,"返回",244,319,68,50,clicked,15);
    button(panel,entering_ssid?"下一步":"连接",8,390,304,54,clicked,16);
}
void select_network(int n) {
    memset(&edit,0,sizeof(edit));value.clear();last_key=-1;
    entering_ssid=n<0;show_password=false;mode=0;
    if(n>=0 && unsigned(n)<ap_count) {
        snprintf(edit.ssid,sizeof(edit.ssid),"%s",aps[n].ssid);
        for(unsigned i=0;i<saved.count;i++)if(!strcmp(saved.p[i].ssid,edit.ssid))value=saved.p[i].password;
    }
    keyboard();
}
void selected(lv_event_t *e) {select_network(int(intptr_t(lv_event_get_user_data(e))));}
void rescan(lv_event_t *){request=SCAN;}
void ap_screen() {
    page=AP_LIST;
    clear_panel();label(panel,"连接 Wi-Fi（2.4 GHz）",8,10);
    label(panel,scanning?"正在搜索附近网络…":"点击网络，输入密码后连接",8,42);
    auto list=lv_obj_create(panel);lv_obj_set_pos(list,0,78);lv_obj_set_size(list,320,310);
    lv_obj_set_style_pad_all(list,4,0);
    for(unsigned i=0;i<ap_count;i++) {
        bool known=false;for(unsigned j=0;j<saved.count;j++)if(!strcmp((char *)aps[i].ssid,saved.p[j].ssid))known=true;
        char text[110];snprintf(text,sizeof(text),"%s  %d dBm%s%s",aps[i].ssid,aps[i].rssi,
            aps[i].authmode==WIFI_AUTH_OPEN?" 开放":"",known?" 已保存":"");
        auto b=button(list,text,0,i*53,292,48,selected,i);
        auto l=lv_obj_get_child(b,0);lv_obj_set_width(l,276);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
    }
    button(panel,"刷新",8,408,140,48,rescan,0);button(panel,"隐藏网络",160,408,152,48,selected,-1);
}
void show() {
    if(!panel || !bsp_display_lock(500))return;
    visible=true;lv_obj_remove_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(panel);ap_screen();bsp_display_unlock();
    p4_media_pause();bsp_display_backlight_on();
}
void events(void *,esp_event_base_t base,int32_t id,void *data) {
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP)got_ip=true;
    if(base!=WIFI_EVENT)return;
    if(id==WIFI_EVENT_SCAN_DONE)scan_done=true;
    if(id==WIFI_EVENT_STA_DISCONNECTED){reason=static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
        if(reason==WIFI_REASON_ASSOC_LEAVE && ignore_leave.exchange(false))return;
        disconnected=true;}
}
void persist() {
    if(!bsp_display_lock(500))return;
    int index=-1;for(unsigned i=0;i<saved.count;i++)if(!strcmp(saved.p[i].ssid,pending.ssid))index=i;
    if(persisted && index>=0 && !strcmp(saved.p[index].password,pending.password)){bsp_display_unlock();return;}
    if(index<0){if(saved.count==8){memmove(saved.p,saved.p+1,7*sizeof(Profile));saved.count=7;}index=saved.count++;}
    saved.p[index]=pending;Saved snapshot=saved;memset(cooldown,0,sizeof(cooldown));bsp_display_unlock();nvs_handle_t h;
    if(nvs_open("p4_wifi",NVS_READWRITE,&h)==ESP_OK){esp_err_t e=nvs_set_blob(h,"profiles",&snapshot,sizeof(snapshot));
        if(e==ESP_OK)e=nvs_commit(h);
        persisted=e==ESP_OK;
        nvs_close(h);printf("@wifi_profiles saved=%u result=%d\n",unsigned(saved.count),int(e));}
}
bool connect(const Profile &profile,bool user,int index) {
    if(!profile.ssid[0])return false;
    wifi_config_t config={};memcpy(config.sta.ssid,profile.ssid,strlen(profile.ssid));
    memcpy(config.sta.password,profile.password,strlen(profile.password));
    config.sta.threshold.authmode=profile.password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    // Scan results already rank saved SSIDs. FAST/strongest selects the best
    // BSSID for that SSID, without roaming away from a healthy connection.
    config.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_scan_stop();scanning=false;scan_done=false;
    ignore_leave=network_online;esp_wifi_disconnect();
    if(esp_wifi_set_config(WIFI_IF_STA,&config)!=ESP_OK)return false;
    pending=profile;manual=user;candidate=index;disconnected=false;got_ip=false;
    joining=esp_wifi_connect()==ESP_OK;join_deadline=now()+20000;return joining;
}
}
extern "C" void p4_wifi_ui_init(void) {
    // Called with the display lock already held, after the shared CJK font loads.
    panel=lv_obj_create(lv_layer_top());lv_obj_set_size(panel,320,480);lv_obj_set_pos(panel,0,0);
    lv_obj_set_style_pad_all(panel,0,0);lv_obj_set_style_border_width(panel,0,0);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x10151e),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(panel,lv_color_white(),0);
    lv_obj_set_style_text_font(panel,static_cast<const lv_font_t *>(p4_visual_font()),0);
    lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
}
extern "C" bool p4_wifi_visible(void){return visible.load();}
extern "C" void p4_wifi_configure(const char *ssid,const char *password) {
    if(!initialized){
        initialized=true;offline_since=now();nvs_handle_t h;
        if(nvs_open("p4_wifi",NVS_READONLY,&h)==ESP_OK){Saved loaded;size_t n=sizeof(loaded);
            if(nvs_get_blob(h,"profiles",&loaded,&n)==ESP_OK && n==sizeof(loaded) && loaded.version==1 && loaded.count<=8){
                bool valid=true;for(unsigned i=0;i<loaded.count;i++)valid&=loaded.p[i].ssid[32]==0 && loaded.p[i].password[64]==0;
                if(valid){saved=loaded;persisted=true;}
            }nvs_close(h);
        }
        esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,events,nullptr);
        esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,events,nullptr);
        // Import the original serial-provisioned network, but save only after IP.
        if(!saved.count && ssid && *ssid){pending={};snprintf(pending.ssid,sizeof(pending.ssid),"%s",ssid);
            snprintf(pending.password,sizeof(pending.password),"%s",password?password:"");saved.p[0]=pending;saved.count=1;}
        next_scan=0;return;
    }
    if(ssid && *ssid){Profile p{};snprintf(p.ssid,sizeof(p.ssid),"%s",ssid);snprintf(p.password,sizeof(p.password),"%s",password?password:"");connect(p,true,-1);}
}
extern "C" void p4_wifi_poll(bool online,bool paused) {
    if(!initialized)return;
    network_online=online;
    auto at=now();Request action=NONE;Profile wanted{};
    if(request!=NONE && panel && bsp_display_lock(500)){action=request.exchange(NONE);wanted=queued;memset(&queued,0,sizeof(queued));bsp_display_unlock();}
    if(paused!=paused_before){joining=false;scanning=false;scan_done=false;next_scan=0;paused_before=paused;}
    if(action==SCAN)next_scan=0;
    if(action==CONNECT && !paused){if(!connect(wanted,true,-1)){manual=false;next_scan=at+3000;show();}return;}
    if(got_ip.exchange(false) && online) {
        wifi_ap_record_t ap{};
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK && !strcmp((char *)ap.ssid,pending.ssid))persist();
        joining=false;manual=false;candidate=-1;offline_since=0;
        if(panel && bsp_display_lock(500)){visible=false;page=AP_LIST;value.clear();memset(&edit,0,sizeof(edit));lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);bsp_display_unlock();}
    }
    if(online){offline_since=0;return;}
    if(!offline_since)offline_since=at;
    if(at-offline_since>=30000 && !visible)show();
    if(paused)return;
    if(joining && at>=join_deadline){esp_wifi_disconnect();disconnected=true;}
    if(disconnected.exchange(false) && joining) {
        joining=false;if(candidate>=0)cooldown[candidate]=at+30000;
        if(manual){manual=false;if(panel && bsp_display_lock(500)){
            keyboard();lv_label_set_text(info,"连接失败，请检查密码后重试");bsp_display_unlock();}}
        next_scan=at+1500;
    }
    if(scan_done.exchange(false)) {
        scanning=false;uint16_t n=24;
        static wifi_ap_record_t found[24];
        if(esp_wifi_scan_get_ap_records(&n,found)==ESP_OK && bsp_display_lock(500)){
            std::sort(found,found+n,[](auto &a,auto &b){return a.rssi>b.rssi;});
            unsigned count=0;for(unsigned i=0;i<n;i++){bool duplicate=!found[i].ssid[0];
                for(unsigned j=0;j<count;j++)duplicate|=!strcmp((char *)found[i].ssid,(char *)aps[j].ssid);
                if(!duplicate)aps[count++]=found[i];}ap_count=count;
            if(visible && panel && page==AP_LIST)ap_screen();
            bsp_display_unlock();
            for(unsigned i=0;i<ap_count && !joining && (!visible || page==AP_LIST);i++)for(unsigned j=0;j<saved.count;j++)
                if(at>=cooldown[j] && !strcmp((char *)aps[i].ssid,saved.p[j].ssid)) {connect(saved.p[j],false,j);break;}
        }
        next_scan=at+10000;
    }
    if(!joining && !scanning && at>=next_scan) {
        wifi_scan_config_t config={};config.show_hidden=true;
        scanning=esp_wifi_scan_start(&config,false)==ESP_OK;next_scan=at+10000;
    }
}
extern "C" int p4_wifi_debug(const char *cmd,char *out,size_t cap) {
    if(!strcmp(cmd,"show")){show();return 0;}
    int index;char tail;
    if(sscanf(cmd,"select %d %c",&index,&tail)==1 && index>=-1 && index<int(ap_count) && bsp_display_lock(100)) {
        select_network(index);bsp_display_unlock();return 0;
    }
    if(!strcmp(cmd,"aps")) {
        if(!bsp_display_lock(100))return 3;
        auto r=cJSON_CreateArray();
        for(unsigned i=0;i<ap_count;i++){auto ap=cJSON_CreateObject();cJSON_AddNumberToObject(ap,"index",i);
            cJSON_AddStringToObject(ap,"ssid",(char *)aps[i].ssid);cJSON_AddNumberToObject(ap,"rssi",aps[i].rssi);cJSON_AddItemToArray(r,ap);}
        char *text=cJSON_PrintUnformatted(r);if(text){snprintf(out,cap,"{\"aps\":%s}",text);cJSON_free(text);}cJSON_Delete(r);bsp_display_unlock();return 0;
    }
    if(!strcmp(cmd,"status")){
        if(!bsp_display_lock(100))return 3;
        snprintf(out,cap,"{\"visible\":%s,\"saved\":%u,\"aps\":%u,\"joining\":%s,\"scanning\":%s,\"last_reason\":%d,\"editor_length\":%u}",visible?"true":"false",unsigned(saved.count),ap_count,joining?"true":"false",scanning?"true":"false",reason.load(),unsigned(value.size()));
        bsp_display_unlock();return 0;
    }
    unsigned code;char extra;
    if(sscanf(cmd,"key %u %c",&code,&extra)==1 && code<=16 && bsp_display_lock(100)){
        if(entry)key(code);
        bsp_display_unlock();return 0;}
    return 1;
}
