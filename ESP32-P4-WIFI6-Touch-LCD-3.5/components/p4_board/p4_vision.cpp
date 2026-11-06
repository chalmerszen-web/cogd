#include "bsp/esp-bsp.h"
#include "driver/jpeg_decode.h"
#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/idf_additions.h"
#include "human_face_detect.hpp"
#include "p4_agent.h"
#include "p4_document_state.h"
#include "p4_capture_quality.hpp"
#include "p4_math_text.hpp"
#include "p4_media.h"
#include "pedestrian_detect.hpp"
#define LODEPNG_NO_COMPILE_CPP
#include "cJSON.h"
#include "src/libs/lodepng/lodepng.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <memory>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <unistd.h>

// The CJK bitmap exceeds the small format's 20-bit (1 MiB) index.
static_assert(LV_FONT_FMT_TXT_LARGE, "Enable CONFIG_LV_FONT_FMT_TXT_LARGE for cjk16.bin");

namespace {
constexpr int W = 320, H = 480;
std::atomic<unsigned> epoch{0};
std::atomic<int> owner{0}, cam_mode{0};
std::atomic<unsigned> capture_frames{0}, inference_ms{0}, camera_errors{0};
std::atomic<int> camera_width{0}, camera_height{0};
std::atomic<unsigned> camera_frame_generation{0};
lv_obj_t *surface = nullptr, *canvas = nullptr, *log_label = nullptr;
lv_obj_t *document = nullptr;
std::atomic<unsigned> document_step{0},document_manual_until{0};
// All non-atomic document state is protected by the display lock. The callback
// never takes job_lock, avoiding inversion with cloud status readers.
unsigned document_question=0,document_revision=0,document_generation=0;
bool document_ready=false,document_restored=false;
unsigned display_render_frames=0;
p4_document_bookmark_t document_bookmark{};
uint16_t *snapshot = nullptr; // state_lock; copied only by capture owner
unsigned snapshot_generation = 0;
bool snapshot_ready = false;
bool snapshot_has_candidate = false;
bool snapshot_measuring = false;
unsigned snapshot_request = 0;
uint64_t snapshot_started_ms = 0;
struct CaptureStatus {
    unsigned generation = 0, samples = 0, selected_elapsed_ms = 0;
    unsigned elapsed_ms = 0, audio_skips = 0;
    unsigned last_sample_elapsed_ms = 0, last_measure_us = 0, max_measure_us = 0;
    unsigned last_copy_us = 0, max_copy_us = 0, last_frame_age_ms = 0, drain_ms = 0;
    const char *state = "idle", *reason = "no_capture";
    p4_capture::Metrics latest{}, selected{};
    bool preview_frozen = false;
} capture_status; // state_lock; no image data or dynamic allocations
std::atomic<bool> question_camera{false};
std::atomic<unsigned> question_frozen_generation{0};
lv_font_t *cjk_font = nullptr;
lv_font_t ui_font;
int still_x = 0, still_y = 0, still_w = W, still_h = H;
uint16_t *pixels = nullptr;
SemaphoreHandle_t state_lock = nullptr;
char camera_state[1536] = "{\"state\":\"off\"}";
TaskHandle_t camera_task = nullptr;
const char *modes[] = {"off", "preview", "person", "approach", "face", "attention", "recognition"};
const char *str(const cJSON *o, const char *k) {
    auto v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : nullptr;
}
int number(const cJSON *o, const char *k, int fallback = 0) {
    auto v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}
uint16_t rgb(int v) { return uint16_t(((v >> 8) & 0xf800) | ((v >> 5) & 0x07e0) | ((v >> 3) & 31)); }
std::string display_text(const char *text) {
    return p4_math::display(text,[](uint32_t cp){
        lv_font_glyph_dsc_t glyph{};
        return lv_font_get_glyph_dsc(&ui_font,&glyph,cp,0) && !glyph.is_placeholder;
    });
}
struct FrameQuality {
    unsigned mean, range;
    bool usable() const { return mean >= 16 && mean <= 247 && range >= 24; }
};
FrameQuality frame_quality(const uint16_t *data, int w, int h) {
    unsigned sum=0,count=0,lo=255,hi=0;
    for(int y=0;y<h;y+=std::max(1,h/32))for(int x=0;x<w;x+=std::max(1,w/32)) {
        unsigned p=data[y*w+x];
        unsigned l=(((p>>11)&31)*8+((p>>5)&63)*8+(p&31)*8)/4;
        sum+=l;++count;lo=std::min(lo,l);hi=std::max(hi,l);
    }
    return {count?sum/count:0,hi-lo};
}
void dot(int x, int y, uint16_t c) {
    if (x >= 0 && x < W && y >= 0 && y < H)
        pixels[y * W + x] = c;
}
void line(int x0, int y0, int x1, int y1, uint16_t c, int width = 1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        for (int y = -width / 2; y <= width / 2; y++)
            for (int x = -width / 2; x <= width / 2; x++)
                dot(x0 + x, y0 + y, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e = err * 2;
        if (e >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
void rect(int x, int y, int w, int h, uint16_t c, bool fill) {
    if (fill) {
        for (int yy = std::max(y, 0); yy < std::min(y + h, H); yy++)
            for (int xx = std::max(x, 0); xx < std::min(x + w, W); xx++)
                dot(xx, yy, c);
    } else {
        line(x, y, x + w - 1, y, c, 2);
        line(x, y + h - 1, x + w - 1, y + h - 1, c, 2);
        line(x, y, x, y + h - 1, c, 2);
        line(x + w - 1, y, x + w - 1, y + h - 1, c, 2);
    }
}
void capture_document_position() {
    if(!document || !document_ready)return;
    int y=lv_obj_get_scroll_y(document);unsigned step=0,paragraph=0;
    int paragraph_y=lv_obj_get_child_count(document)?lv_obj_get_y(lv_obj_get_child(document,0)):0;
    for(unsigned i=0;i<lv_obj_get_child_count(document);++i) {
        auto child=lv_obj_get_child(document,i);int top=lv_obj_get_y(child);
        unsigned id=unsigned(uintptr_t(lv_obj_get_user_data(child)));
        if(id && top<=y+80)step=id;
        if(top<=y){paragraph=i;paragraph_y=top;}
    }
    document_step=step;
    if(document_question && document_revision)
        document_bookmark={document_question,document_revision,step,paragraph,y,y-paragraph_y,true};
}
bool refresh_document() {
    lv_obj_invalidate(document);
    // The adapter also calls lv_refr_now under its display lock. RENDER_READY
    // acknowledges a software frame, not physical panel scan-out.
    return p4_refresh_transaction(document_ready,display_render_frames,document_generation,epoch,owner,
        []{lv_refr_now(lv_obj_get_display(document));},[]{return lv_obj_is_visible(document);});
}
void clear_labels() {
    capture_document_position();
    document_ready=false;document_restored=false;
    document_question=document_revision=document_generation=0;
    document = nullptr;
    document_step=0;
    lv_obj_clean(surface);
    canvas = lv_canvas_create(surface);
    lv_canvas_set_buffer(canvas, pixels, W, H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_remove_flag(canvas,LV_OBJ_FLAG_CLICKABLE);
    log_label = lv_label_create(surface);
    lv_obj_set_width(log_label, 304);
    lv_obj_set_pos(log_label, 8, 330);
    lv_obj_set_style_text_font(log_label, &ui_font, 0);
    lv_obj_set_style_text_color(log_label, lv_color_white(), 0);
    lv_label_set_text(log_label, "");
    lv_obj_remove_flag(log_label,LV_OBJ_FLAG_CLICKABLE);
}
inline __attribute__((always_inline)) uint16_t blend565(uint16_t a, uint16_t b, unsigned weight) {
    // Separate red and blue before 8-bit weighting; the native RGB565 gap
    // is too small and fractional red bits would otherwise leak into blue.
    unsigned ar = ((a & 0xf800) << 5) | (a & 31);
    unsigned br = ((b & 0xf800) << 5) | (b & 31);
    unsigned rb = (ar * (256 - weight) + br * weight) >> 8;
    unsigned g = ((a & 0x07e0) * (256 - weight) + (b & 0x07e0) * weight) >> 8;
    return ((rb >> 5) & 0xf800) | (rb & 31) | (g & 0x07e0);
}
void resize_camera(const uint16_t *src, int sw, int sh, uint16_t *dst, int dw, int dh) {
    // Pixel-centred bilinear sampling avoids the old top-left point-sampling
    // shimmer. No full-resolution copy or allocation on the preview path.
    int xs[W];
    for (int x = 0; x < dw; ++x)
        xs[x] = std::clamp((2*x+1)*sw*128/dw-128, 0, (sw-1)*256);
    for (int y = 0; y < dh; ++y) {
        int sy = std::clamp((2*y+1)*sh*128/dh-128, 0, (sh-1)*256);
        const uint16_t *a = src+(sy>>8)*sw, *b = src+std::min((sy>>8)+1,sh-1)*sw;
        for (int x = 0; x < dw; ++x) {
            int sx = xs[x]>>8, nx = std::min(sx+1,sw-1);
            dst[y*dw+x] = blend565(blend565(a[sx],a[nx],xs[x]&255),
                                  blend565(b[sx],b[nx],xs[x]&255),sy&255);
        }
    }
}
bool present_camera(const uint16_t *src, int sw, int sh, uint16_t *scaled,
                    const std::list<dl::detect::result_t> &boxes, const char *message,
                    unsigned generation, unsigned &render_us, unsigned &lock_us,
                    bool captured_frame = false) {
    if (!p4_visual_current(generation)) return false;
    int dh = std::min(300, sh * W / sw), dw = sw * dh / sh, ox = (W - dw) / 2, oy = 20;
    int64_t began = esp_timer_get_time();
    resize_camera(src,sw,sh,scaled,dw,dh);
    render_us = unsigned(esp_timer_get_time()-began);
    if (!bsp_display_lock(10)) return false;
    began = esp_timer_get_time();
    if (owner.load() != 1 || !p4_visual_current(generation) ||
        (question_camera && question_frozen_generation.load()==generation && !captured_frame)) {
        bsp_display_unlock();
        return false;
    }
    for (int y = 0; y < dh; y++)
        memcpy(pixels+(y+oy)*W+ox,scaled+y*dw,dw*sizeof(*pixels));
    for (const auto &r : boxes) {
        if (r.box.size() < 4)
            continue;
        rect(ox + r.box[0] * dw / sw, oy + r.box[1] * dh / sh, (r.box[2] - r.box[0]) * dw / sw,
             (r.box[3] - r.box[1]) * dh / sh, 0x07e0, false);
        for (size_t i = 0; i + 1 < r.keypoint.size(); i += 2)
            rect(ox + r.keypoint[i] * dw / sw - 1, oy + r.keypoint[i + 1] * dh / sh - 1, 3, 3, 0xf800, true);
    }
    if (!question_camera && strcmp(lv_label_get_text(log_label),message)) lv_label_set_text(log_label, message);
    lv_area_t area = {ox,oy,ox+dw-1,oy+dh-1};
    lv_obj_invalidate_area(canvas,&area);
    lock_us = unsigned(esp_timer_get_time()-began);
    bsp_display_unlock();
    return true;
}
void publish(const char *s) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    snprintf(camera_state, sizeof(camera_state), "%s", s);
    xSemaphoreGive(state_lock);
}
bool forward_face(const dl::detect::result_t &r) {
    if (r.keypoint.size() != 10)
        return false;
    float ex = (r.keypoint[0] + r.keypoint[6]) / 2.f, ey = (r.keypoint[1] + r.keypoint[7]) / 2.f;
    float span = std::abs(r.keypoint[6] - r.keypoint[0]);
    return span > 18 && std::abs(r.keypoint[4] - ex) < span * .22f &&
           std::abs(r.keypoint[1] - r.keypoint[7]) < span * .25f && r.keypoint[5] > ey;
}
bool any_forward_face(const std::list<dl::detect::result_t> &results) {
    // A higher-confidence profile must not hide another face looking toward the camera.
    return std::any_of(results.begin(), results.end(), forward_face);
}

// One in-flight inference frame, no queue of stale camera pictures. The
// capture owner alone publishes results and owns the lifetime of this worker.
struct CameraInference {
    TaskHandle_t task = nullptr;
    std::atomic<bool> finished{false}, quit{false}, alive{false};
    dl::image::img_t image = {};
    std::list<dl::detect::result_t> results;
    int mode = 0;
    unsigned elapsed_us = 0;
    int64_t sampled_at = 0;
};
void inference_worker(void *arg) {
    auto &job = *static_cast<CameraInference *>(arg);
    {
        std::unique_ptr<HumanFaceDetect> face;
        std::unique_ptr<PedestrianDetect> person;
        for (;;) {
            ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
            if (job.quit.load()) break;
            int64_t began = esp_timer_get_time();
            job.results.clear();
            if (job.mode == 2 || job.mode == 3) {
                if (!person) { person = std::make_unique<PedestrianDetect>(); person->set_score_thr(0.5f); }
                job.results = person->run(job.image);
            } else {
                if (!face) face = std::make_unique<HumanFaceDetect>();
                job.results = face->run(job.image);
            }
            job.elapsed_us = unsigned(esp_timer_get_time()-began);
            job.finished = true;
        }
    } // Release model arenas before the capture owner frees the input image.
    job.alive = false;
    vTaskDeleteWithCaps(nullptr);
}

void capture_worker(void *) {
    static p4_capture::Window capture_window; // two small grids stay off the task stack
    for (;;) {
        while (!cam_mode.load())
            vTaskDelay(pdMS_TO_TICKS(100));
        unsigned generation = epoch.load();
        int fd = -1, type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        bool initialized = false, streaming = false;
        uint16_t *buffers[2] = {};
        char failure[100] = "camera initialization failed";
        CameraInference inference;
        std::unique_ptr<uint16_t,decltype(&free)> scaled(nullptr,free), input(nullptr,free);
        esp_video_init_csi_config_t csi = {};
        csi.sccb_config.init_sccb = false;
        csi.sccb_config.i2c_handle = bsp_i2c_get_handle();
        csi.sccb_config.freq = 100000;
        csi.reset_pin = GPIO_NUM_NC;
        csi.pwdn_pin = GPIO_NUM_NC;
        esp_video_init_config_t init = {};
        init.csi = &csi;
        esp_err_t ret =
            esp_video_init_with_flags(&init, ESP_VIDEO_INIT_FLAGS_MIPI_CSI | ESP_VIDEO_INIT_FLAGS_ISP);
        struct v4l2_format format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ret != ESP_OK) {
            snprintf(failure, sizeof(failure), "sensor init: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        initialized = true;
        fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
        if (fd < 0)
            goto cleanup;
        if (ioctl(fd, VIDIOC_G_FMT, &format))
            goto cleanup;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(fd, VIDIOC_S_FMT, &format))
            goto cleanup;
        camera_width = format.fmt.pix.width;
        camera_height = format.fmt.pix.height;
        scaled.reset(static_cast<uint16_t *>(heap_caps_malloc(W*300*2,MALLOC_CAP_SPIRAM)));
        if (!scaled) { snprintf(failure,sizeof(failure),"preview allocation failed"); goto cleanup; }
        inference.mode = cam_mode.load();
        if (inference.mode > 1) {
            input.reset(static_cast<uint16_t *>(heap_caps_malloc(camera_width*camera_height*2,MALLOC_CAP_SPIRAM)));
            if (!input) { snprintf(failure,sizeof(failure),"inference allocation failed"); goto cleanup; }
            inference.image.data = input.get();
            inference.image.width = camera_width;
            inference.image.height = camera_height;
            inference.image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
            inference.alive = true;
            if (xTaskCreatePinnedToCoreWithCaps(inference_worker,"p4_detect",8192,&inference,1,
                    &inference.task,0,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) != pdPASS) {
                inference.alive = false;
                snprintf(failure,sizeof(failure),"inference worker allocation failed"); goto cleanup;
            }
        }
        {
            struct v4l2_requestbuffers req = {};
            req.count = 2;
            req.type = type;
            req.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_REQBUFS, &req))
                goto cleanup;
        }
        for (int i = 0; i < 2; i++) {
            struct v4l2_buffer b = {};
            b.index = i;
            b.type = type;
            b.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_QUERYBUF, &b))
                goto cleanup;
            buffers[i] =
                (uint16_t *)mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
            if (!buffers[i] || buffers[i] == MAP_FAILED || ioctl(fd, VIDIOC_QBUF, &b))
                goto cleanup;
        }
        if (ioctl(fd, VIDIOC_STREAMON, &type))
            goto cleanup;
        streaming = true;
        {
            int64_t started = esp_timer_get_time(), next_preview = started, last_infer = 0, last_log = 0;
            int64_t last_area_time = 0, result_time = 0;
            float previous_area = 0;
            int previous_x = 0, previous_y = 0;
            unsigned stable = 0, received = 0, shown = 0, inferred = 0;
            unsigned render_us = 0, lock_us = 0, max_lock_us = 0;
            bool pending = false, facing = false, approaching = false;
            std::list<dl::detect::result_t> results, no_boxes;
            char message[450] = "摄像头启动中…";
            while (cam_mode.load() && p4_visual_current(generation)) {
                struct v4l2_buffer b = {};
                b.type = type;
                b.memory = V4L2_MEMORY_MMAP;
                if (ioctl(fd, VIDIOC_DQBUF, &b)) {
                    snprintf(failure, sizeof(failure), "capture dequeue failed");
                    break;
                }
                if (b.index >= 2) {
                    snprintf(failure, sizeof(failure), "invalid capture buffer");
                    break;
                }
                int64_t now = esp_timer_get_time();
                int mode = cam_mode.load();
                if (b.flags & V4L2_BUF_FLAG_DONE) {
                    ++received;
                    camera_frame_generation=generation;
                    int iw = camera_width, ih = camera_height;
                    unsigned request=0;uint64_t requested_at=0;
                    if(xSemaphoreTake(state_lock,0)==pdTRUE) {
                        if(snapshot && !snapshot_ready && snapshot_generation==generation) {
                            if(p4_media_audio_reserve())++capture_status.audio_skips;
                            else {
                                request=snapshot_request;requested_at=snapshot_started_ms;
                                // Finalization must not accept an older candidate while
                                // a newer movement/blur sample is still being evaluated.
                                snapshot_measuring=true;
                            }
                        }
                        xSemaphoreGive(state_lock);
                    }
                    if(request) {
                        if(capture_window.request!=request)capture_window.begin(request,requested_at);
                        // Score before the expensive preview resize/display. Keep the
                        // dequeue time as frame time, never the processing finish time.
                        int64_t measure_at=esp_timer_get_time();
                        auto decision=capture_window.observe(buffers[b.index],iw,ih,uint64_t(now/1000));
                        unsigned measure_us=unsigned(esp_timer_get_time()-measure_at);
                        xSemaphoreTake(state_lock,portMAX_DELAY);
                        if(snapshot_request==request) {
                            if(snapshot && snapshot_generation==generation && decision.sampled) {
                                capture_status.samples=capture_window.samples;
                                capture_status.latest=capture_window.latest;
                                capture_status.reason=capture_window.reason;
                                capture_status.last_sample_elapsed_ms=unsigned(uint64_t(now/1000)-requested_at);
                                capture_status.last_measure_us=measure_us;
                                capture_status.max_measure_us=std::max(capture_status.max_measure_us,measure_us);
                                if(decision.discard) {
                                    snapshot_has_candidate=false;capture_status.selected={};
                                    capture_status.selected_elapsed_ms=0;
                                }
                                if(decision.replace) {
                                    int64_t copy_at=esp_timer_get_time();
                                    memcpy(snapshot,buffers[b.index],size_t(iw)*ih*2);
                                    capture_status.last_copy_us=unsigned(esp_timer_get_time()-copy_at);
                                    capture_status.max_copy_us=std::max(capture_status.max_copy_us,capture_status.last_copy_us);
                                    snapshot_has_candidate=true;
                                    capture_status.selected=capture_window.best;
                                    capture_status.selected_elapsed_ms=capture_window.selected_elapsed_ms;
                                }
                                capture_status.last_frame_age_ms=unsigned((esp_timer_get_time()-now)/1000);
                                capture_status.elapsed_ms=unsigned(uint64_t(esp_timer_get_time()/1000)-requested_at);
                            }
                            snapshot_measuring=false;
                        }
                        xSemaphoreGive(state_lock);
                    }
                    auto quality = frame_quality(buffers[b.index],iw,ih);
                    if (pending && inference.finished.load()) {
                        results = std::move(inference.results);
                        result_time = inference.sampled_at;
                        inference_ms = inference.elapsed_us/1000;
                        ++inferred; pending = false;
                        facing = approaching = false;
                        if (!results.empty()) {
                            const auto &r = results.front();
                            int area = r.box_area();
                            /* MSRMNP points are eye, mouth, nose, eye, mouth. This is a head-direction hint,
                             * never measured eye gaze. */
                            if (mode == 5) {
                                facing = any_forward_face(results);
                                stable = facing ? stable + 1 : 0;
                                facing = stable >= 3;
                            }
                            if (mode == 3 && r.box.size() == 4) {
                                int cx = (r.box[0] + r.box[2]) / 2, cy = (r.box[1] + r.box[3]) / 2;
                                approaching = previous_area > 0 && area > previous_area * 1.30f &&
                                              abs(cx - previous_x) < iw / 5 &&
                                              abs(cy - previous_y) < ih / 5;
                                if (now - last_area_time > 1500000) {
                                    previous_area = area;
                                    previous_x = cx;
                                    previous_y = cy;
                                    last_area_time = now;
                                }
                            }
                        } else {
                            stable = 0;
                            previous_area = 0;
                        }
                    }
                    if (!quality.usable() || (result_time && now-result_time > 700000)) {
                        results.clear(); result_time = 0;
                        stable = 0; previous_area = 0;
                        facing = approaching = false;
                    }
                    if (inference.task && !pending && quality.usable() && now-last_infer >= 200000 && !p4_media_audio_reserve()) {
                        memcpy(input.get(),buffers[b.index],size_t(iw)*ih*2);
                        inference.sampled_at = last_infer = now;
                        inference.finished = false;
                        pending = true;
                        xTaskNotifyGive(inference.task);
                    }
                    // Inference is asynchronous. Old overlays expire instead
                    // of pretending that a delayed box follows a moving face.
                    unsigned age = result_time ? unsigned((now-result_time)/1000) : 0;
                    const auto &boxes = result_time ? results : no_boxes;
                    float score = boxes.empty() ? 0 : boxes.front().score;
                    if (boxes.empty()) facing = approaching = false;
                    if (now-last_log >= 200000) {
                        last_log = now;
                        char state[1536];
                        snprintf(state, sizeof(state),
                                 "{\"state\":\"running\",\"mode\":\"%s\",\"sensor\":\"OV5647\",\"width\":%d,"
                                 "\"height\":%d,\"frames\":%u,\"detections\":%u,\"score\":%.3f,\"inference_ms\":%"
                                 "u,\"face_toward_candidate\":%s,\"eye_gaze\":\"not_measured\",\"approaching_"
                                 "candidate\":%s,\"identity\":\"not_enrolled\",\"image_usable\":%s,\"mean_luma\":%u,\"luma_range\":%u,"
                                 "\"preview_fps\":%.1f,\"received_frames\":%u,\"inferences\":%u,\"result_age_ms\":%u,"
                                 "\"resize_us\":%u,\"display_lock_us\":%u,\"max_display_lock_us\":%u,\"heap_free\":%u}",
                                 modes[mode], iw, ih, capture_frames.load(),
                                 (unsigned)boxes.size(), score, mode>1?inference_ms.load():0, facing ? "true" : "false",
                                 approaching ? "true" : "false",
                                 quality.usable()?"true":"false",quality.mean,quality.range,
                                 shown*1000000.0/std::max<int64_t>(now-started,1),received,inferred,age,
                                 render_us,lock_us,max_lock_us,
                                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
                        snprintf(message, sizeof(message),
                                 "摄像头  %s   %.1f fps\n目标 %u  置信度 %.2f  推理 %u ms\n%s", modes[mode],
                                 shown*1000000.0/std::max<int64_t>(now-started,1),
                                 (unsigned)boxes.size(), score, mode>1?inference_ms.load():0,
                                 mode == 5   ? (facing ? "面朝镜头候选\n眼球视线尚未测定"
                                                       : "未确认面朝镜头\n眼球视线尚未测定")
                                 : mode == 3 ? (approaching ? "检测到靠近趋势" : "未检测到靠近趋势")
                                 : mode == 6 ? "人脸检测基础\n未录入身份，不能识别人名"
                                             : "本地检测 / 实时画面");
                        if(!quality.usable())snprintf(message,sizeof(message),
                            "摄像头测试  %s\n画面过暗、过曝或缺少细节\n当前不能判断是否有人\n请检查镜头和光线",modes[mode]);
                        if (mode > 0 && cam_mode.load() == mode && p4_visual_current(generation))
                            publish(state);
                    }
                    if (now >= next_preview && !p4_media_audio_reserve() &&
                        !(question_camera && question_frozen_generation.load()==generation)) {
                        next_preview += 66667;
                        if (next_preview < now) next_preview = now+66667;
                        if (present_camera(buffers[b.index],iw,ih,scaled.get(),boxes,message,generation,render_us,lock_us)) {
                            ++capture_frames; ++shown;
                            max_lock_us = std::max(max_lock_us,lock_us);
                        }
                    }

                }
                if (ioctl(fd, VIDIOC_QBUF, &b))
                    break;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    cleanup:
        if (inference.task) {
            inference.quit = true;
            xTaskNotifyGive(inference.task);
            while (inference.alive.load()) vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (streaming)
            ioctl(fd, VIDIOC_STREAMOFF, &type);
        if (fd >= 0)
            close(fd);
        if (initialized)
            esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_MIPI_CSI | ESP_VIDEO_INIT_FLAGS_ISP);
        if (cam_mode.load() && p4_visual_current(generation)) {
            camera_errors++;
            cam_mode = 0;
            char state[200];
            snprintf(state, sizeof(state), "{\"state\":\"error\",\"error\":\"%s\"}", failure);
            publish(state);
            p4_visual_message(generation, failure);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int draw_check(const cJSON *r) {
    auto background = cJSON_GetObjectItemCaseSensitive(r, "background");
    if (background && (!cJSON_IsNumber(background) || background->valuedouble != background->valueint ||
                       background->valueint < 0 || background->valueint > 0xffffff))
        return 1;
    auto brightness = cJSON_GetObjectItemCaseSensitive(r, "brightness");
    if (brightness && (!cJSON_IsNumber(brightness) || brightness->valuedouble != brightness->valueint ||
                       brightness->valueint < 1 || brightness->valueint > 100))
        return 1;
    auto ops = cJSON_GetObjectItemCaseSensitive(r, "objects");
    if (!cJSON_IsArray(ops) || cJSON_GetArraySize(ops) > 40)
        return 1;
    const cJSON *o;
    cJSON_ArrayForEach(o, ops) {
        const char *type = str(o, "type");
        if (!type)
            return 1;
        if (strcmp(type, "rect") && strcmp(type, "circle") && strcmp(type, "line") && strcmp(type, "text"))
            return 1;
        for (auto k : {"x", "x2", "y", "y2", "w", "h", "r", "width"}) {
            auto v = cJSON_GetObjectItemCaseSensitive(o, k);
            if (v &&
                (!cJSON_IsNumber(v) || v->valuedouble != v->valueint || v->valueint < 0 || v->valueint > 480))
                return 1;
        }
        if (number(o, "x") >= W || number(o, "x2") >= W || number(o, "y") >= H || number(o, "y2") >= H)
            return 1;
        if (number(o, "width", 1) < 1 || number(o, "width", 1) > 16 || number(o, "r") > 240)
            return 1;
        auto color = cJSON_GetObjectItemCaseSensitive(o, "color");
        if (color && (!cJSON_IsNumber(color) || color->valuedouble != color->valueint ||
                      color->valueint < 0 || color->valueint > 0xffffff))
            return 1;
        if (!strcmp(type, "text") && (!str(o, "text") || strlen(str(o, "text")) > 240))
            return 1;
    }
    return 0;
}
} // namespace

extern "C" void p4_visual_init(void) {
    jpeg_decode_memory_alloc_cfg_t memory = {};
    memory.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t allocated = 0;
    pixels = (uint16_t *)jpeg_alloc_decoder_mem(W * H * 2, &memory, &allocated);
    if (pixels) memset(pixels, 0, W * H * 2);
    state_lock = xSemaphoreCreateMutex();
    if (!pixels || !state_lock || !bsp_display_lock(1000))
        return;
    auto assets=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"media");
    uint32_t header[4]={};const void *font=nullptr;static esp_partition_mmap_handle_t font_map;
    if(assets && esp_partition_read(assets,0,header,sizeof(header))==ESP_OK && header[0]==0x31464743 &&
       header[1]==1 && header[2]>0 && header[2]<=0x17f000 &&
       esp_partition_mmap(assets,4096,header[2],ESP_PARTITION_MMAP_DATA,&font,&font_map)==ESP_OK &&
       esp_rom_crc32_le(0,static_cast<const uint8_t *>(font),header[2])==header[3])
        cjk_font = lv_binfont_create_from_buffer(const_cast<void *>(font),header[2]);
    ui_font = *LV_FONT_DEFAULT;
    ui_font.fallback = cjk_font;
    if(cjk_font) {
        // Fallback glyphs can be taller than Montserrat. Reserve their line
        // metrics too, otherwise multiline Chinese camera logs overlap.
        ui_font.line_height=std::max(ui_font.line_height,cjk_font->line_height);
        ui_font.base_line=std::max(ui_font.base_line,cjk_font->base_line);
    }
    if (!cjk_font)
        ESP_LOGE("p4_visual", "CJK font allocation failed");
    surface = lv_obj_create(lv_screen_active());
    lv_display_add_event_cb(lv_obj_get_display(surface),[](lv_event_t *){++display_render_frames;},LV_EVENT_RENDER_READY,nullptr);
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, W, H);
    lv_obj_set_pos(surface, 0, 0);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(surface,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(surface,[](lv_event_t *){
        if(owner.load()==4 && !p4_wifi_visible())p4_media_touch(epoch.load());
    },LV_EVENT_CLICKED,nullptr);
    clear_labels();
    lv_obj_add_flag(surface, LV_OBJ_FLAG_HIDDEN);
    p4_wifi_ui_init();
    bsp_display_unlock();
    xTaskCreatePinnedToCore(capture_worker, "p4_camera", 8192, nullptr, 2, &camera_task, 1);
}
extern "C" const void *p4_visual_font(void) { return &ui_font; }
extern "C" bool p4_visual_active(void) { return owner.load() != 0 || p4_wifi_visible(); }
extern "C" bool p4_visual_current(unsigned e) { return e && e == epoch.load(); }
extern "C" unsigned p4_visual_begin(int mode) {
    unsigned e=p4_begin_transaction(epoch,owner,mode,[]{return surface && bsp_display_lock(1000);},[](unsigned){
        cam_mode=0;question_camera=false;question_frozen_generation=0;
        clear_labels();
        std::fill(pixels,pixels+W*H,uint16_t(0));
        lv_obj_remove_flag(surface,LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(canvas);
        bsp_display_unlock();
    });
    if(!e)return 0;
    if(mode!=1 && state_lock)publish("{\"state\":\"off\"}");
    bsp_display_backlight_on();
    return e;
}
extern "C" void p4_visual_stop(void) {
    cam_mode = 0;
    question_camera = false;
    question_frozen_generation = 0;
    if(state_lock)publish("{\"state\":\"off\"}");
    owner = 0;
    ++epoch;
    if (surface && bsp_display_lock(1000)) {
        capture_document_position();
        lv_obj_add_flag(surface, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }
}
extern "C" void p4_visual_message(unsigned e, const char *message) {
    if (!p4_visual_current(e) || !surface || !bsp_display_lock(500))
        return;
    if(!p4_visual_current(e)){bsp_display_unlock();return;}
    auto shown=display_text(message);
    lv_label_set_text(log_label, shown.c_str());
    lv_obj_invalidate(canvas);
    bsp_display_unlock();
}
extern "C" void p4_visual_brightness(int percent) { bsp_display_brightness_set(std::clamp(percent, 1, 100)); }

extern "C" unsigned p4_camera_question_begin(void) {
    unsigned e=p4_visual_begin(1);if(!e)return 0;question_camera=true;cam_mode=1;
    p4_visual_message(e,"请把题目放在镜头前\n保持文字清晰、光线充足");return e;
}
extern "C" int p4_camera_capture_status(char *out,size_t cap) {
    if(!out || !cap || !state_lock)return 1;
    xSemaphoreTake(state_lock,portMAX_DELAY);auto status=capture_status;xSemaphoreGive(state_lock);
    const auto &q=!strcmp(status.state,"ready")?status.selected:status.latest;
    int n=snprintf(out,cap,
        "{\"state\":\"%s\",\"reason\":\"%s\",\"generation\":%u,\"samples\":%u,"
        "\"elapsed_ms\":%u,\"selected_elapsed_ms\":%u,\"mean_luma\":%u,\"luma_range\":%u,"
        "\"sharpness\":%u,\"edge_samples\":%u,\"sharp_edges\":%u,\"sharp_fraction\":%u,"
        "\"motion\":%u,\"changed_permille\":%u,\"audio_skips\":%u,\"preview_frozen\":%s,"
        "\"gradient_points\":%u,\"gradient_stride\":%u,\"last_sample_elapsed_ms\":%u,"
        "\"last_measure_us\":%u,\"max_measure_us\":%u,\"last_copy_us\":%u,\"max_copy_us\":%u,"
        "\"last_frame_age_ms\":%u,\"drain_ms\":%u}",
        status.state,status.reason,status.generation,status.samples,status.elapsed_ms,status.selected_elapsed_ms,
        q.mean,q.range,q.sharpness,q.edge_samples,q.sharp_edges,q.sharp_fraction,q.motion,q.changed_permille,
        status.audio_skips,status.preview_frozen?"true":"false",q.gradient_points,q.stride,
        status.last_sample_elapsed_ms,status.last_measure_us,status.max_measure_us,
        status.last_copy_us,status.max_copy_us,status.last_frame_age_ms,status.drain_ms);
    return n<0 || size_t(n)>=cap?5:0;
}
extern "C" int p4_camera_jpeg(unsigned e,uint8_t **data,size_t *size) {
    *data=nullptr;*size=0;
    if(!p4_visual_current(e) || !cam_mode)return 4;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if(snapshot){xSemaphoreGive(state_lock);return 3;}
    capture_status={};capture_status.generation=e;capture_status.state="collecting";
    capture_status.reason="camera_initializing";question_frozen_generation=0;xSemaphoreGive(state_lock);
    int64_t init_until=esp_timer_get_time()+5000000;
    while(p4_visual_current(e) && cam_mode && camera_frame_generation.load()!=e && esp_timer_get_time()<init_until)
        vTaskDelay(pdMS_TO_TICKS(20));
    if(!p4_visual_current(e))return 4;
    int w=camera_width.load(),h=camera_height.load();
    if(!cam_mode || camera_frame_generation.load()!=e || w<=0 || h<=0) {
        xSemaphoreTake(state_lock,portMAX_DELAY);capture_status.state="rejected";
        capture_status.reason="camera_initializing_timeout";xSemaphoreGive(state_lock);return 3;
    }
    jpeg_encode_memory_alloc_cfg_t alloc={};alloc.buffer_direction=JPEG_ENC_ALLOC_INPUT_BUFFER;
    size_t capacity=0;
    auto rgb24=(uint8_t *)jpeg_alloc_encoder_mem(size_t(w)*h*3,&alloc,&capacity);
    auto copy=(uint16_t *)heap_caps_malloc(size_t(w)*h*2,MALLOC_CAP_SPIRAM);
    if(!copy || !rgb24){free(copy);free(rgb24);xSemaphoreTake(state_lock,portMAX_DELAY);
        capture_status.state="rejected";capture_status.reason="capture_allocation_failed";xSemaphoreGive(state_lock);return 19;}
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if(snapshot){xSemaphoreGive(state_lock);free(copy);free(rgb24);return 3;}
    snapshot=copy;snapshot_ready=false;snapshot_has_candidate=false;snapshot_measuring=false;snapshot_generation=e;
    if(!++snapshot_request)++snapshot_request;
    snapshot_started_ms=uint64_t(esp_timer_get_time()/1000);
    capture_status={};capture_status.generation=e;capture_status.state="collecting";capture_status.reason="no_frames";
    question_frozen_generation=0;
    xSemaphoreGive(state_lock);
    bool ready=false;
    for(int i=0;i<100 && p4_visual_current(e);++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if(uint64_t(esp_timer_get_time()/1000)-snapshot_started_ms>=p4_capture::window_ms)break;
    }
    // Drain only a score that is already in flight at the window boundary.
    // It may invalidate the old candidate. Its actual dequeue timestamp still
    // has to pass the same age gate; waiting cannot make an old image fresh.
    int64_t drain_started=esp_timer_get_time();
    for(;;) {
        xSemaphoreTake(state_lock,portMAX_DELAY);
        if(!snapshot_measuring || !p4_visual_current(e) ||
           esp_timer_get_time()-drain_started>=int64_t(p4_capture::measure_drain_ms)*1000)break;
        xSemaphoreGive(state_lock);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    capture_status.drain_ms=unsigned((esp_timer_get_time()-drain_started)/1000);
    unsigned elapsed=unsigned(uint64_t(esp_timer_get_time()/1000)-snapshot_started_ms);
    bool recent=capture_status.samples && p4_capture::fresh_frame_times(
        elapsed,capture_status.last_sample_elapsed_ms,capture_status.selected_elapsed_ms);
    bool sample_recent=capture_status.samples && p4_capture::fresh_frame_times(
        elapsed,capture_status.last_sample_elapsed_ms,capture_status.last_sample_elapsed_ms);
    ready=capture_status.samples && p4_capture::final_frame_ready(snapshot_has_candidate,snapshot_measuring,
        elapsed,capture_status.last_sample_elapsed_ms,capture_status.selected_elapsed_ms) && p4_visual_current(e);
    snapshot_ready=ready;snapshot=nullptr;
    capture_status.elapsed_ms=elapsed;
    capture_status.state=ready?"selected":"rejected";
    if(!p4_visual_current(e))capture_status.reason="superseded";
    else if(ready)capture_status.reason="ok";
    else if(snapshot_measuring)capture_status.reason="measurement_timeout";
    else if(!capture_status.samples)capture_status.reason=capture_status.audio_skips?"audio_busy":"no_frames";
    else if(!sample_recent)capture_status.reason="sample_gap";
    else if(snapshot_has_candidate && !recent)capture_status.reason="candidate_stale";
    xSemaphoreGive(state_lock);
    if(!ready || !p4_visual_current(e)){free(copy);free(rgb24);return p4_visual_current(e)?3:4;}
    if(question_camera) {
        // The caller owns copy throughout this display operation. Never hold
        // state_lock while acquiring the display lock. Freeze the exact source
        // frame rather than a later live preview that the reader never received.
        auto scaled=static_cast<uint16_t *>(heap_caps_malloc(W*300*2,MALLOC_CAP_SPIRAM));
        bool allocated=scaled!=nullptr,shown=false;
        unsigned render_us=0,lock_us=0;std::list<dl::detect::result_t> no_boxes;
        if(allocated) {
            question_frozen_generation=e;
            for(unsigned retry=0;retry<3 && p4_visual_current(e);++retry) {
                shown=present_camera(copy,w,h,scaled,no_boxes,"",e,render_us,lock_us,true);
                if(shown)break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            free(scaled);
        }
        xSemaphoreTake(state_lock,portMAX_DELAY);
        capture_status.preview_frozen=shown;
        if(!shown){capture_status.state="rejected";capture_status.reason=allocated?"preview_unavailable":"preview_allocation_failed";}
        xSemaphoreGive(state_lock);
        if(!shown) {
            unsigned held=e;question_frozen_generation.compare_exchange_strong(held,0);
            free(copy);free(rgb24);return p4_visual_current(e)?(allocated?3:19):4;
        }
    }
    for(int i=0;i<w*h;++i){unsigned c=copy[i];rgb24[3*i]=((c>>11)&31)*255/31;rgb24[3*i+1]=((c>>5)&63)*255/63;rgb24[3*i+2]=(c&31)*255/31;}
    free(copy);alloc.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER;size_t out_size=0;
    auto jpg=(uint8_t *)jpeg_alloc_encoder_mem(size_t(w)*h*2,&alloc,&out_size);
    jpeg_encoder_handle_t encoder=nullptr;jpeg_encode_engine_cfg_t cfg={};cfg.timeout_ms=1500;
    int error=jpg?0:19;uint32_t used=0;
    if(!error && jpeg_new_encoder_engine(&cfg,&encoder)!=ESP_OK)error=16;
    jpeg_encode_cfg_t format={};format.width=w;format.height=h;format.src_type=JPEG_ENCODE_IN_FORMAT_RGB888;
    format.sub_sample=JPEG_DOWN_SAMPLING_YUV444;format.image_quality=90;
    if(!error && jpeg_encoder_process(encoder,&format,rgb24,w*h*3,jpg,out_size,&used)!=ESP_OK)error=16;
    if(encoder)jpeg_del_encoder_engine(encoder);
    free(rgb24);
    if(!error && !p4_visual_current(e))error=4;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if(capture_status.generation==e) {
        capture_status.state=error?"rejected":"ready";
        capture_status.reason=error==4?"superseded":error?"jpeg_encode_failed":"ok";
        if(error)capture_status.preview_frozen=false;
    }
    xSemaphoreGive(state_lock);
    if(error){unsigned held=e;question_frozen_generation.compare_exchange_strong(held,0);free(jpg);return error;}
    *data=jpg;*size=used;return 0;
}
extern "C" int p4_visual_document(unsigned e,const char *text) {
    return p4_visual_document_ex(e,text,0,0,false);
}
extern "C" int p4_visual_document_ex(unsigned e,const char *text,unsigned question,unsigned revision,bool restore) {
    if(!text || !*text)return 1;
    if(!p4_visual_current(e))return 4;
    if(!surface || !bsp_display_lock(1500))return 15;
    if(!p4_visual_current(e) || owner.load()!=5){bsp_display_unlock();return 4;}
    clear_labels();lv_obj_add_flag(canvas,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(log_label,LV_OBJ_FLAG_HIDDEN);
    // Keep the bookmark local while LVGL sends layout/scroll callbacks.
    auto saved=document_bookmark;
    document=lv_obj_create(surface);
    if(!document){bsp_display_unlock();return 19;}
    document_question=question;document_revision=revision;document_generation=e;
    lv_obj_set_size(document,W,H);lv_obj_set_pos(document,0,0);
    lv_obj_set_style_bg_color(document,lv_color_hex(0xf8f8f4),0);
    lv_obj_set_style_radius(document,0,0);lv_obj_set_style_border_width(document,0,0);
    lv_obj_set_style_pad_all(document,12,0);lv_obj_set_style_pad_row(document,0,0);
    lv_obj_set_flex_flow(document,LV_FLEX_FLOW_COLUMN);lv_obj_set_scroll_dir(document,LV_DIR_VER);
    lv_obj_remove_flag(document,LV_OBJ_FLAG_SCROLL_ELASTIC);lv_obj_set_scrollbar_mode(document,LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(document,[](lv_event_t *event){
        auto code=lv_event_get_code(event);unsigned now=unsigned(esp_timer_get_time()/1000);
        if(code==LV_EVENT_PRESSED || code==LV_EVENT_PRESSING){document_manual_until=now+1500;p4_tutor_activity();}
        if(code!=LV_EVENT_SCROLL && code!=LV_EVENT_SCROLL_END)return;
        if(int32_t(document_manual_until.load()-now)>0)p4_tutor_activity();
        capture_document_position();
    },LV_EVENT_ALL,nullptr);
    /* Small paragraph labels avoid a giant glyph/layout buffer. No fixed
     * page count: content determines the height; native LVGL clamps edges. */
    // Convert a display-only copy before splitting labels: a script expression
    // crossing the 768-byte label boundary must still form one ^(...)/_(...).
    auto shown=display_text(text);
    const char *at=shown.c_str();unsigned step=0;
    while(*at) {
        if(!strncmp(at,"【步骤",strlen("【步骤")))step=unsigned(strtoul(at+strlen("【步骤"),nullptr,10));
        size_t n=strlen(at);if(n>768){n=768;while(n && (uint8_t(at[n])&0xc0)==0x80)--n;
            size_t k=n;while(k>n/2 && at[k-1]!='\n')--k;if(k>n/2)n=k;}
        if(auto next=strstr(at+1,"\n【"))if(size_t(next+1-at)<n)n=next+1-at;
        std::string part(at,n);auto label=lv_label_create(document);
        if(!label){lv_obj_delete(document);document=nullptr;bsp_display_unlock();return 19;}
        lv_obj_set_user_data(label,reinterpret_cast<void *>(uintptr_t(step)));lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(label,W-24);lv_obj_set_style_text_font(label,&ui_font,0);
        lv_obj_set_style_text_color(label,lv_color_hex(0x151515),0);lv_obj_set_style_text_line_space(label,6,0);
        lv_label_set_text(label,part.c_str());at+=n;
    }
    lv_obj_update_layout(document);
    int maximum=std::max(0,int(lv_obj_get_scroll_y(document)+lv_obj_get_scroll_bottom(document)));
    int paragraph_y=-1;
    if(saved.paragraph<lv_obj_get_child_count(document))paragraph_y=lv_obj_get_y(lv_obj_get_child(document,saved.paragraph));
    int target=p4_bookmark_target(&saved,question,revision,restore,paragraph_y,maximum);
    document_restored=restore && p4_bookmark_matches(&saved,question,revision);
    lv_obj_scroll_to_y(document,target,LV_ANIM_OFF);
    lv_obj_remove_flag(surface,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(document,LV_OBJ_FLAG_HIDDEN);
    if(!refresh_document()){int error=p4_visual_current(e)?15:4;bsp_display_unlock();return error;}
    capture_document_position();
    ESP_LOGI("p4_visual","document question=%u revision=%u generation=%u render_frame=%u restored=%d scroll_y=%d step=%u",
             question,revision,e,display_render_frames,document_restored,int(lv_obj_get_scroll_y(document)),document_step.load());
    bsp_display_unlock();bsp_display_backlight_on();return 0;
}
extern "C" bool p4_document_view(unsigned question,unsigned revision,p4_document_view_t *out) {
    if(!out)return false;
    *out={};
    if(!surface || !bsp_display_lock(300))return false;
    bool match=document && document_question==question && document_revision==revision;
    if(match) {
        bool rendered=document_ready && p4_visual_current(document_generation);
        bool visible=p4_document_is_visible(document_generation,epoch.load(),owner.load(),document_ready,
                                            !lv_obj_is_visible(document) || p4_wifi_visible());
        *out={visible,rendered,document_restored,document_generation,document_question,document_revision,
              document_step.load(),int(lv_obj_get_scroll_y(document))};
    }
    bsp_display_unlock();return match;
}
extern "C" void p4_document_forget(unsigned question) {
    if(!bsp_display_lock(1000))return;
    if(!question || document_bookmark.question_id==question)document_bookmark={};
    // Do not let destruction/scroll callbacks resurrect a discarded lesson.
    if(!question || document_question==question)document_question=document_revision=0;
    bsp_display_unlock();
}
extern "C" int p4_document_scroll(int delta,char *out,size_t cap) {
    if(!bsp_display_lock(1000))return 15;
    int error=0;
    if(!document || !document_ready || !p4_visual_current(document_generation))error=2;
    else {
        lv_obj_update_layout(document);int y=lv_obj_get_scroll_y(document);
        int maximum=std::max(0,y+int(lv_obj_get_scroll_bottom(document)));
        y=std::clamp(y+delta,0,maximum);lv_obj_scroll_to_y(document,y,LV_ANIM_OFF);
        if(delta && !refresh_document()){bsp_display_unlock();return 15;}
        capture_document_position();
        if(delta){document_manual_until=unsigned(esp_timer_get_time()/1000)+1500;p4_tutor_activity();}
        snprintf(out,cap,"{\"scroll_y\":%d,\"scroll_max\":%d,\"at_top\":%s,\"at_bottom\":%s}",y,maximum,y==0?"true":"false",y==maximum?"true":"false");
    }
    bsp_display_unlock();return error;
}
extern "C" unsigned p4_document_step(void) {return document_step.load();}
extern "C" int p4_document_focus(unsigned step,char *out,size_t cap) {
    if(!bsp_display_lock(300))return 15;
    int error=2;unsigned now=unsigned(esp_timer_get_time()/1000);
    bool manual=int32_t(document_manual_until.load()-now)>0;
    if(document && document_ready && p4_visual_current(document_generation)) {
        if(manual){snprintf(out,cap,"{\"focused\":false,\"manual_scrolling\":true}");error=0;}
        else for(unsigned i=0;i<lv_obj_get_child_count(document);++i) {
            auto child=lv_obj_get_child(document,i);
            if(unsigned(uintptr_t(lv_obj_get_user_data(child)))!=step)continue;
            lv_obj_scroll_to_y(document,lv_obj_get_y(child),LV_ANIM_OFF);document_step=step;
            if(!refresh_document()){error=15;break;}
            capture_document_position();
            snprintf(out,cap,"{\"focused\":true,\"step\":%u}",step);error=0;break;
        }
    }
    bsp_display_unlock();return error;
}

extern "C" int p4_vision_fixture(unsigned generation, const char *mode, char *out, size_t cap) {
    auto copy = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    if (!copy)
        return 19;
    if (!bsp_display_lock(1000)) {
        free(copy);
        return 15;
    }
    if (!p4_visual_current(generation)) {
        bsp_display_unlock();
        free(copy);
        return 4;
    }
    int ox = still_x, oy = still_y, iw = still_w, ih = still_h;
    for (int y = 0; y < ih; y++)
        memcpy(copy + y * iw, pixels + (y + oy) * W + ox, iw * 2);
    bsp_display_unlock();
    dl::image::img_t input = {};
    input.data = copy;
    input.width = iw;
    input.height = ih;
    input.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
    auto quality=frame_quality(copy,iw,ih);
    std::list<dl::detect::result_t> results;
    int64_t began = esp_timer_get_time();
    if (quality.usable() && !strcmp(mode, "person")) {
        PedestrianDetect model;
        model.set_score_thr(0.5f);
        results = model.run(input);
    } else if(quality.usable()) {
        HumanFaceDetect model;
        results = model.run(input);
    }
    free(copy);
    unsigned elapsed = (esp_timer_get_time() - began) / 1000;
    if (!p4_visual_current(generation))
        return 4;
    auto status = cJSON_CreateObject();
    cJSON_AddStringToObject(status, "state", "fixture_complete");
    cJSON_AddStringToObject(status, "source", "test_image_not_camera");
    cJSON_AddStringToObject(status, "mode", mode);
    cJSON_AddStringToObject(status, "box_space", "fitted_image_without_letterbox");
    cJSON_AddNumberToObject(status, "detections", results.size());
    cJSON_AddNumberToObject(status, "inference_ms", elapsed);
    cJSON_AddBoolToObject(status,"image_usable",quality.usable());
    cJSON_AddNumberToObject(status,"mean_luma",quality.mean);
    cJSON_AddNumberToObject(status,"luma_range",quality.range);
    cJSON_AddBoolToObject(status, "face_toward_single_frame",
                          strcmp(mode, "person") && any_forward_face(results));
    cJSON_AddStringToObject(status, "eye_gaze", "not_measured");
    auto boxes = cJSON_AddArrayToObject(status, "boxes");
    for (const auto &r : results) {
        auto item = cJSON_CreateObject();
        cJSON_AddItemToArray(boxes, item);
        cJSON_AddNumberToObject(item, "score", r.score);
        cJSON_AddItemToObject(item, "xyxy", cJSON_CreateIntArray(r.box.data(), r.box.size()));
        cJSON_AddItemToObject(item, "keypoints", cJSON_CreateIntArray(r.keypoint.data(), r.keypoint.size()));
        if (cJSON_GetArraySize(boxes) >= 6)
            break;
    }
    char *json = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);
    int err = !json ? 19 : strlen(json) >= cap ? 5 : 0;
    if (!err) {
        strcpy(out, json);
        publish(out);
    }
    cJSON_free(json);
    if (!bsp_display_lock(1000))
        return 15;
    if (p4_visual_current(generation)) {
        for (const auto &r : results)
            if (r.box.size() == 4)
                rect(r.box[0] + ox, r.box[1] + oy, r.box[2] - r.box[0], r.box[3] - r.box[1], 0x07e0, false);
        char label[160];
        snprintf(label, sizeof(label), "测试图片 / 非实时采集\n%s 目标 %u / %u ms", mode,
                 (unsigned)results.size(), elapsed);
        lv_label_set_text(log_label, label);
        lv_obj_invalidate(canvas);
    }
    bsp_display_unlock();
    return err;
}

extern "C" int p4_visual_image(unsigned e, const uint8_t *data, size_t size) {
    if(p4_media_background_wait(e))return 4;
    auto jpeg_decode = [&](uint8_t *target, size_t bytes, jpeg_dec_output_format_t format) {
        jpeg_decoder_handle_t handle = nullptr;
        jpeg_decode_engine_cfg_t cfg = {};
        cfg.timeout_ms = 2000;
        esp_err_t err = jpeg_new_decoder_engine(&cfg, &handle);
        jpeg_decode_cfg_t fmt = {};
        fmt.output_format = format;
        // IDF's RGB565 RGB setting swaps the two packed bytes. LVGL's canvas
        // holds native little-endian uint16_t pixels; RGB888 still needs RGB.
        fmt.rgb_order = format == JPEG_DECODE_OUT_FORMAT_RGB565 ?
            JPEG_DEC_RGB_ELEMENT_ORDER_BGR : JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
        uint32_t used = 0;
        if (!err) err = jpeg_decoder_process(handle, &fmt, data, size, target, bytes, &used);
        if (handle) jpeg_del_decoder_engine(handle);
        return err;
    };
    unsigned char *decoded = nullptr;
    lv_draw_buf_t *png_buffer = nullptr;
    auto release = [&] {
        if (png_buffer)
            lv_draw_buf_destroy(png_buffer);
        else
            free(decoded);
    };
    unsigned w = 0, h = 0;
    bool rgba = true;
    int stride = 0;
    if (size >= 24 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8)) {
        w = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
        h = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
        if (!w || !h || w > 2048 || h > 2048 || uint64_t(w) * h > 2200000)
            return 5;
        // LVGL ships a modified LodePNG: output is a draw-buffer handle, not pixel bytes.
        unsigned char *handle = nullptr;
        unsigned error = lodepng_decode32(&handle, &w, &h, data, size);
        png_buffer = reinterpret_cast<lv_draw_buf_t *>(handle);
        decoded = png_buffer ? png_buffer->data : nullptr;
        if (error) {
            ESP_LOGE("p4_visual", "PNG decode error %u, bytes %u", error, (unsigned)size);
            release();
            return error == 83 ? 19 : 7;
        }
        if (!decoded) {
            release();
            return 19;
        }
        stride = png_buffer->header.stride / 4;
    } else {
        jpeg_decode_picture_info_t info = {};
        if (jpeg_decoder_get_info(data, size, &info) != ESP_OK)
            return 7;
        w = info.width;
        h = info.height;
        if (!w || !h || w > 2048 || h > 2048 || uint64_t(w) * h > 2200000)
            return 5;
        if (w == W && h == H) {
            if (!bsp_display_lock(1000)) return 15;
            int result = 4;
            if (p4_visual_current(e)) {
                result = jpeg_decode(reinterpret_cast<uint8_t *>(pixels), W * H * 2,
                                     JPEG_DECODE_OUT_FORMAT_RGB565) == ESP_OK ? 0 : 20;
                if (!result) {
                    still_x = still_y = 0; still_w = W; still_h = H;
                    lv_label_set_text(log_label, "");lv_obj_invalidate(canvas);
                }
            }
            bsp_display_unlock();return result;
        }
        stride = (w + 15) & ~15;
        size_t allocated = 0;
        jpeg_decode_memory_alloc_cfg_t mem = {};
        mem.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
        decoded = (uint8_t *)jpeg_alloc_decoder_mem(stride * ((h + 15) & ~15) * 3, &mem, &allocated);
        if (!decoded)
            return 19;
        esp_err_t err = jpeg_decode(decoded, allocated, JPEG_DECODE_OUT_FORMAT_RGB888);
        if (err) {
            release();
            return 20;
        }
        rgba = false;
    }
    if (!p4_visual_current(e)) {
        release();
        return 4;
    }
    if (!bsp_display_lock(1000)) {
        release();
        return 15;
    }
    int dw = W, dh = int(h) * W / int(w);
    if (dh > H) {
        dh = H;
        dw = int(w) * H / int(h);
    }
    int ox = (W - dw) / 2, oy = (H - dh) / 2;
    still_x = ox;
    still_y = oy;
    still_w = dw;
    still_h = dh;
    std::fill(pixels, pixels + W * H, uint16_t(0));
    int channels = rgba ? 4 : 3;
    if (!rgba && dw == int(w) && dh == int(h)) {
        // Native-size video already has its aspect fit. Avoid per-pixel division.
        for (int y = 0; y < dh; ++y) {
            auto p = decoded + y * stride * 3;
            auto target = pixels + (y + oy) * W + ox;
            for (int x = 0; x < dw; ++x, p += 3)
                target[x] = uint16_t(((p[0] & 248) << 8) | ((p[1] & 252) << 3) | (p[2] >> 3));
        }
    } else for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            auto p = decoded + (y * h / dh * stride + x * w / dw) * channels;
            unsigned a = rgba ? p[3] : 255;
            pixels[(y + oy) * W + x + ox] =
                rgb(((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255));
        }
    lv_label_set_text(log_label, "");
    lv_obj_invalidate(canvas);
    bsp_display_unlock();
    release();
    return 0;
}

extern "C" int p4_agent_check(const char *tool, const char *args) {
    if (strcmp(tool, "device_camera_test") && strcmp(tool, "device_display_draw"))
        return p4_cloud_check(tool, args);
    cJSON *r = cJSON_Parse(args);
    if (!r)
        return 6;
    int err = 1;
    if (!strcmp(tool, "device_camera_test")) {
        const char *m = str(r, "mode");
        if (m) {
            if (!strcmp(m, "status"))
                err = 0;
            for (auto v : modes)
                if (!strcmp(m, v))
                    err = 0;
            if (auto url = str(r, "test_image_url")) {
                if ((strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) || strlen(url) > 2048 ||
                    (strcmp(m, "person") && strcmp(m, "face") && strcmp(m, "attention") &&
                     strcmp(m, "recognition")))
                    err = 1;
            }
            if (auto b = cJSON_GetObjectItemCaseSensitive(r, "brightness"))
                if (!cJSON_IsNumber(b) || b->valuedouble != b->valueint || b->valueint < 1 ||
                    b->valueint > 100)
                    err = 1;
        }
    } else
        err = draw_check(r);
    cJSON_Delete(r);
    return err;
}
extern "C" int p4_agent_call(const char *tool, const char *args, char *out, size_t cap) {
    int err = p4_agent_check(tool, args);
    if (err)
        return err;
    if (strcmp(tool, "device_camera_test") && strcmp(tool, "device_display_draw"))
        return p4_cloud_call(tool, args, out, cap);
    if (!surface || !state_lock)
        return 19;
    cJSON *r = cJSON_Parse(args);
    if (!r)
        return 6;
    if (!strcmp(tool, "device_camera_test")) {
        const char *mode = str(r, "mode");
        if (str(r, "test_image_url")) {
            cJSON_Delete(r);
            return p4_cloud_call(tool, args, out, cap);
        }
        if (!strcmp(mode, "status")) {
            xSemaphoreTake(state_lock, portMAX_DELAY);
            snprintf(out, cap, "%s", camera_state);
            xSemaphoreGive(state_lock);
        } else if (!strcmp(mode, "off")) {
            p4_visual_stop();
            publish("{\"state\":\"off\"}");
            snprintf(out, cap, "{\"state\":\"off\"}");
        } else {
            unsigned e = p4_visual_begin(1);
            if(!e){cJSON_Delete(r);return 15;}
            p4_visual_brightness(number(r, "brightness", 100));
            int m = 1;
            for (int i = 1; i < 7; i++)
                if (!strcmp(mode, modes[i]))
                    m = i;
            publish("{\"state\":\"starting\"}");
            p4_visual_message(e, "正在启动摄像头…");
            cam_mode = m;
            snprintf(out, cap, "{\"state\":\"starting\",\"mode\":\"%s\",\"generation\":%u}", mode, e);
        }
    } else {
        unsigned e = p4_visual_begin(2);
        if(!e){cJSON_Delete(r);return 15;}
        p4_visual_brightness(number(r, "brightness", 100));
        if (!bsp_display_lock(1000)) {
            cJSON_Delete(r);
            return 15;
        }
        if(!p4_visual_current(e)){bsp_display_unlock();cJSON_Delete(r);return 4;}
        std::fill(pixels, pixels + W * H, rgb(number(r, "background", 0)));
        auto ops = cJSON_GetObjectItemCaseSensitive(r, "objects");
        const cJSON *o;
        cJSON_ArrayForEach(o, ops) {
            auto type = str(o, "type");
            int x = number(o, "x"), y = number(o, "y"), w = number(o, "w"), h = number(o, "h"),
                color = number(o, "color", 0xffffff);
            bool fill = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o, "fill"));
            if (!strcmp(type, "rect"))
                rect(x, y, w, h, rgb(color), fill);
            if (!strcmp(type, "line"))
                line(x, y, number(o, "x2"), number(o, "y2"), rgb(color), number(o, "width", 1));
            if (!strcmp(type, "circle")) {
                int radius = number(o, "r");
                for (int yy = std::max(-radius, -y); yy <= std::min(radius, H - y - 1); yy++)
                    for (int xx = std::max(-radius, -x); xx <= std::min(radius, W - x - 1); xx++) {
                        int rr = xx * xx + yy * yy;
                        if (rr <= radius * radius && (fill || rr >= (radius - 2) * (radius - 2)))
                            dot(x + xx, y + yy, rgb(color));
                    }
            }
            if (!strcmp(type, "text")) {
                auto label = lv_label_create(surface);
                lv_obj_set_style_text_font(label, &ui_font, 0);
                lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
                lv_obj_set_pos(label, x, y);
                lv_obj_set_width(label, W - x);
                lv_label_set_text(label, str(o, "text"));
            }
        }
        lv_obj_invalidate(canvas);
        bsp_display_unlock();
        snprintf(out, cap,
                 "{\"state\":\"rendered\",\"generation\":%u,\"objects\":%d,\"width\":320,\"height\":480}", e,
                 cJSON_GetArraySize(ops));
    }
    cJSON_Delete(r);
    return 0;
}
