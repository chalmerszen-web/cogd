#include "p4_status_hint.h"
#include <assert.h>
int main(void)
{
    p4_status_hint_t s={0};
    p4_status_hint_update(&s,"growing","","好");
    p4_status_hint_update(&s,"growing","","好了");
    p4_status_hint_update(&s,"growing","","好了没");
    assert(p4_status_hint_matches(&s,"growing"));
    s=(p4_status_hint_t){0};
    /* candidate138 status fault: provisional phonetic error was revised. */
    p4_status_hint_update(&s,"revised","","he喽");
    assert(!p4_status_hint_matches(&s,"revised"));
    p4_status_hint_update(&s,"revised","","好了没");
    assert(p4_status_hint_matches(&s,"revised"));
    s=(p4_status_hint_t){0};
    /* Recorded candidate137 query11: full tentative phrase, empty final. */
    p4_status_hint_update(&s,"item_MX5NKHVUtx5cLGyEa9Wx4","","好了没");
    assert(p4_status_hint_matches(&s,"item_MX5NKHVUtx5cLGyEa9Wx4"));
    assert(!p4_status_hint_matches(&s,"previous_item"));
    assert(!p4_status_hint_matches(&s,""));
    /* A later revision must revoke the hint, not leave a sticky keyword. */
    p4_status_hint_update(&s,"item_MX5NKHVUtx5cLGyEa9Wx4","","好了没，再加一");
    assert(!p4_status_hint_matches(&s,"item_MX5NKHVUtx5cLGyEa9Wx4"));
    p4_status_hint_update(&s,"item_MX5NKHVUtx5cLGyEa9Wx4","","好了没");
    assert(!p4_status_hint_matches(&s,"item_MX5NKHVUtx5cLGyEa9Wx4"));
    const char *reject[]={"不要回答好了没","他说‘好了没’","取消拍题","重新拍照","7加5等于多少","好了"};
    for(unsigned i=0;i<sizeof(reject)/sizeof(*reject);++i){
        s=(p4_status_hint_t){0};
        p4_status_hint_update(&s,"current","",reject[i]);
        assert(!p4_status_hint_matches(&s,"current"));
    }
    s=(p4_status_hint_t){0};
    p4_status_hint_update(&s,"current","好了","没");
    assert(!p4_status_hint_matches(&s,"current"));
    s=(p4_status_hint_t){0};
    p4_status_hint_update(&s,"current","好了吗？","");
    assert(p4_status_hint_matches(&s,"current"));
    s=(p4_status_hint_t){0}; /* New local utterance. */
    assert(!p4_status_hint_matches(&s,"current"));
    return 0;
}
