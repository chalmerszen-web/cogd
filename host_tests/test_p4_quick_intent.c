#include "p4_quick_intent.h"
#include <assert.h>
int main(void)
{
    const char *yes[]={"好了没"," 好了没？ ","题目解好了吗？","进度怎么样","现在到哪一步了？"};
    const char *no[]={"","还没好","不要问好了没","解释一下好了没是什么意思","好了没？另外七加五是多少","不管好了没，都先别讲","好了没并不重要"};
    for(unsigned i=0;i<sizeof(yes)/sizeof(*yes);++i)assert(p4_quick_progress(yes[i]));
    for(unsigned i=0;i<sizeof(no)/sizeof(*no);++i)assert(!p4_quick_progress(no[i]));
    assert(!p4_quick_progress(NULL));
    const char *capture_yes[]={"拍题","重新拍照。","请用摄像头重新拍一下当前题目。",
        "请帮我用摄像头拍一下这道题！","再拍一张","帮我重拍当前题目"};
    const char *capture_no[]={"","不要重拍","请不要拍题","解释一下重拍是什么意思",
        "如果不清楚就重新拍题","请用摄像头重新拍一下当前题目，然后告诉我七加五",
        "你可以拍题吗","现在好了没","把重拍写下来","“重新拍题”","请请拍题"};
    for(unsigned i=0;i<sizeof(capture_yes)/sizeof(*capture_yes);++i)assert(p4_quick_capture(capture_yes[i]));
    for(unsigned i=0;i<sizeof(capture_no)/sizeof(*capture_no);++i)assert(!p4_quick_capture(capture_no[i]));
    assert(!p4_quick_capture(NULL));
    const char *cancel_yes[]={"取消读题","请取消拍题。","取消这次拍题任务，不用继续识别了。",
        "请帮我取消当前解题任务","停止读题","取消拍题，不用继续了"};
    const char *cancel_no[]={"不要取消拍题","你取消拍题了吗","取消拍题是什么意思",
        "取消读题，然后重新拍照","如果还没好就取消读题","取消录音","请请取消读题",
        "“取消读题”","取消这次拍题任务，不用继续识别了吗"};
    for(unsigned i=0;i<sizeof(cancel_yes)/sizeof(*cancel_yes);++i)assert(p4_quick_cancel(cancel_yes[i]));
    for(unsigned i=0;i<sizeof(cancel_no)/sizeof(*cancel_no);++i)assert(!p4_quick_cancel(cancel_no[i]));
    assert(!p4_quick_cancel(NULL));
    return 0;
}
