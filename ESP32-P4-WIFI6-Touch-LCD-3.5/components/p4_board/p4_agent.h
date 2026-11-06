#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Return cogd agent_err_t values through this C ABI; the DL adapter is C++. */
int p4_agent_check(const char *tool, const char *arguments);
int p4_agent_call(const char *tool, const char *arguments, char *out, size_t cap);
void p4_visual_init(void);
const void *p4_visual_font(void);
void p4_wifi_ui_init(void);
void p4_wifi_configure(const char *ssid,const char *password);
void p4_wifi_poll(bool online,bool paused);
bool p4_wifi_visible(void);
int p4_wifi_debug(const char *cmd,char *out,size_t cap);
bool p4_visual_active(void);
void p4_visual_stop(void);
unsigned p4_visual_begin(int owner);
/* Zero is never a valid generation; begin returns zero on display-lock failure. */
bool p4_visual_current(unsigned generation);
void p4_visual_message(unsigned generation, const char *message);
void p4_visual_brightness(int percent);
int p4_visual_image(unsigned generation, const uint8_t *data, size_t size);
unsigned p4_camera_question_begin(void);
int p4_camera_jpeg(unsigned generation, uint8_t **data, size_t *size);
int p4_camera_capture_status(char *out, size_t cap);
int p4_visual_document(unsigned generation, const char *text);
int p4_visual_document_ex(unsigned generation, const char *text, unsigned question_id, unsigned revision, bool restore);
typedef struct {
    bool visible, rendered, restored;
    unsigned generation, question_id, revision, step;
    int scroll_y;
} p4_document_view_t;
/* False means no matching document or unavailable display lock; out is cleared. */
bool p4_document_view(unsigned question_id, unsigned revision, p4_document_view_t *out);
void p4_document_forget(unsigned question_id); /* zero discards every bookmark */
int p4_document_scroll(int pixels, char *out, size_t cap);
int p4_document_focus(unsigned step, char *out, size_t cap);
unsigned p4_document_step(void);
bool p4_tutor_waiting(void);
void p4_tutor_activity(void);
bool p4_tutor_pause_due(void);
void p4_tutor_context(char *out, size_t cap);
void p4_tutor_status(char *out, size_t cap);
void p4_tutor_user_text(const char *text);
/* USB diagnostics only. JPEG byte offset/count (1..1024); each chunk carries capture_id. */
int p4_tutor_photo_read(unsigned offset,unsigned count,char *out,size_t cap);
bool p4_tutor_claim_notice(unsigned question_id,unsigned revision,char *out,size_t cap);
int p4_vision_fixture(unsigned generation, const char *mode, char *out, size_t cap);
int p4_cloud_check(const char *tool, const char *arguments);
int p4_cloud_call(const char *tool, const char *arguments, char *out, size_t cap);
#ifdef __cplusplus
}
#endif
