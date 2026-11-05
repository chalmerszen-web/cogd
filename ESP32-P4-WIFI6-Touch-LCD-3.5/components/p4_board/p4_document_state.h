#pragma once
#include <stdbool.h>
#include <stdint.h>

/* RAM only. A revision changes layout/content and must not reuse old offsets. */
typedef struct {
    unsigned question_id, revision, step, paragraph;
    int scroll_y, paragraph_offset;
    bool valid;
} p4_document_bookmark_t;

static inline bool p4_bookmark_matches(const p4_document_bookmark_t *b, unsigned question, unsigned revision) {
    return b->valid && question && revision && b->question_id == question && b->revision == revision;
}

static inline int p4_bookmark_target(const p4_document_bookmark_t *b, unsigned question, unsigned revision,
                                    bool restore, int paragraph_y, int maximum) {
    if (!restore || !p4_bookmark_matches(b, question, revision)) return 0;
    int64_t y = paragraph_y >= 0 ? (int64_t)paragraph_y + b->paragraph_offset : b->scroll_y;
    if (y < 0) return 0;
    return y > maximum ? maximum : (int)y;
}

static inline bool p4_document_is_visible(unsigned generation, unsigned current_generation,
                                         int owner, bool ready, bool hidden) {
    return generation && generation == current_generation && owner == 5 && ready && !hidden;
}

#ifdef __cplusplus
#include <atomic>
/* The production begin path and host fault injection share this transaction.
 * No generation, owner or widget mutation is allowed before acquiring the lock. */
template<class Lock, class Activate>
unsigned p4_begin_transaction(std::atomic<unsigned> &generation, std::atomic<int> &owner,
                              int requested_owner, Lock lock, Activate activate) {
    if (!lock()) return 0;
    unsigned next = ++generation;
    if (!next) next = ++generation;
    owner = requested_owner;
    activate(next);
    return next;
}

template<class Refresh, class Visible>
bool p4_refresh_transaction(bool &ready, unsigned &frames, unsigned generation,
                            const std::atomic<unsigned> &current, const std::atomic<int> &owner,
                            Refresh refresh, Visible visible) {
    ready = false;
    unsigned before = frames;
    refresh();
    ready = frames != before && p4_document_is_visible(generation, current.load(), owner.load(), true, !visible());
    return ready;
}
#endif
