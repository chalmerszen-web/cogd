#include "p4_quick_intent.h"
#include <assert.h>
#include <stdio.h>
static void cancellation_authority_cases(void)
{
    /* Actual179 accepted this story request, but the provider chose cancel.
     * Tool choice and a mention of cancellation supply no user authority. */
    const char *denied[]={"讲一个300字的故事。","讲一个关于取消拍题的故事。",
        "请讲一个300字的故事，不要取消拍题。","不要取消拍题","请不要取消读题",
        "别取消解题","请帮我不要取消当前解题任务","不要停止读题",
        "他说取消读题","用户说取消拍题","解释一下取消解题是什么意思",
        "“取消读题”","\"取消拍题\"","'停止读题'","请把取消拍题写下来",
        "如果没好就取消读题","取消拍题还是继续读题","取消拍题了吗",
        "取消读题，然后讲故事","请取消读题，不对，不要取消",
        "取消录音","取消故事","取消","停止","好了没","是的","嗯",""};
    const char *allowed[]={"取消拍题","取消这次拍题任务","取消当前拍题任务",
        "取消读题","停止读题","取消解题","取消当前解题任务",
        "请帮我取消当前解题任务。","帮我取消读题","取消拍题，不用继续识别了。",
        "取消拍题，不用继续了","取消拍题，不要继续识别了","  请取消读题！  "};
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied);++i)assert(!p4_quick_cancel(denied[i]));
    for(unsigned i=0;i<sizeof(allowed)/sizeof(*allowed);++i)assert(p4_quick_cancel(allowed[i]));
    assert(!p4_quick_cancel(NULL));
    /* The original whole-input limit applies before stripping whitespace;
     * no truncated prefix or overlong negative tail may become authority. */
    char boundary[102];memset(boundary,' ',sizeof(boundary));
    memcpy(boundary,"取消读题",strlen("取消读题"));boundary[100]=0;
    assert(p4_quick_cancel(boundary));
    boundary[100]=' ';boundary[101]=0;assert(!p4_quick_cancel(boundary));
    char oversized[512];memset(oversized,'x',sizeof(oversized));
    memcpy(oversized,"取消读题",strlen("取消读题"));oversized[511]=0;
    assert(!p4_quick_cancel(oversized));
    printf("PASS cancel authority explicit=%u denied=%u plus null/whole-input boundaries\n",
        (unsigned)(sizeof(allowed)/sizeof(*allowed)),(unsigned)(sizeof(denied)/sizeof(*denied)));
}
static void acquisition_cases(void)
{
    const char *fresh[]={"拍题","请重新拍照，只读第一题。","好的，请再拍一张题图。",
        "请用摄像头重新拍一下当前题目。","请帮我用摄像头拍一下这道题！",
        "再拍一张","帮我重拍当前题目","请重新拍照读题。","重新拍题，然后只读第二题。",
        "麻烦你重拍这道题，再读第十二道题好吗？","可以重新拍一下镜头前这道题吗？",
        "换一张题图","请拍一张题图，先读第三题吧","请用摄像头看一下这道题",
        "重新拍照，请只读第一题。","拍一下纸上的题目","拍照","帮我拍张照",
        "请再拍一下","请重新拍一下","帮我拍张照片","请拍一张照片"};
    const char *current[]={"看题","帮我看一下镜头前这道题","这题怎么解","这道题怎么做？",
        "这道题我不会做，帮我看看","这道题我看不懂，请讲一下吧",
        "请读一下纸上的这道题","麻烦你分析这道数学题好吗？","请解一下当前题目。",
        "第一题","请看第十二道题","读题","你读题啊","开始读题","可以帮我识别一下这道题吗"};
    /* Actual175 ASR was correct; the provider incorrectly called start.
     * Negative variations check positive authority, not a story blacklist. */
    const char *unrelated[]={"讲一个300字的故事。","讲一个关于数学题的故事。","今天天气怎么样",
        "十二减三等于几","你好","好了没","在吗","嗯。","好的","是的。","对，就是这道题！",
        "是这道题，请开始解题。","不要拍题","请不要拍题","别重新拍照","不要看题",
        "他说拍题","用户说重新拍照","“重新拍题”","\"看题\"","如果我说拍题会怎样",
        "如果不清楚就重新拍题","拍题是什么意思","解释一下重拍是什么意思",
        "告诉我怎么拍题","先讲故事，再讨论怎么拍题","请重拍，然后讲一个故事",
        "请重新拍照，只读第一题，再讲一个故事","请重新拍照，只读第一题，不要读题",
        "不要帮我看一下镜头前这道题","看题还是讲故事","请看题并不要拍照",
        "这道题是什么电影里的台词","这道题我不会做是什么意思","请读第一题的故事",
        "第一题和第二题谁更难","第二大题第一小题","第一题对吗","先讲","我先看一下",
        "这道题","题","请开始解题","请\t开始解题","请\r\n开始解题","请开始解题好吗",
        "我想请你我想请你我想请你我想请你我想请你我想请你我想请你我想请你开始解题",
        "请把拍题写下来","你已经拍题了吗","“拍照”","\"帮我拍张照\"",
        "不要拍照","别帮我拍张照","请再拍一下然后讲故事","拍一下","",
        "我放好了","放好了","再试一次","重新试一次"};
    const char *states[]={"idle","reading","awaiting_confirmation","needs_selection","solving",
        "complete","incomplete","failed","needs_clearer_view","cancelled"};
    unsigned combinations=0;
    for(unsigned i=0;i<sizeof(fresh)/sizeof(*fresh);++i){
        assert(p4_voice_tutor_request(fresh[i],false)==P4_TUTOR_REQUEST_FRESH);
        for(unsigned s=0;s<sizeof(states)/sizeof(*states);++s)for(unsigned photo=0;photo<2;++photo){
            assert(p4_voice_tutor_start_allowed(fresh[i],states[s],photo!=0));++combinations;
        }
    }
    for(unsigned i=0;i<sizeof(current)/sizeof(*current);++i){
        assert(p4_voice_tutor_request(current[i],false)==P4_TUTOR_REQUEST_CURRENT);
        for(unsigned s=0;s<sizeof(states)/sizeof(*states);++s)for(unsigned photo=0;photo<2;++photo){
            bool retry=!strcmp(states[s],"failed") || !strcmp(states[s],"needs_clearer_view");
            bool expected=(!photo || retry) && strcmp(states[s],"reading");
            assert(p4_voice_tutor_start_allowed(current[i],states[s],photo!=0)==expected);++combinations;
        }
    }
    for(unsigned i=0;i<sizeof(unrelated)/sizeof(*unrelated);++i){
        assert(p4_voice_tutor_request(unrelated[i],false)==P4_TUTOR_REQUEST_NONE);
        for(unsigned s=0;s<sizeof(states)/sizeof(*states);++s)for(unsigned photo=0;photo<2;++photo){
            bool retry=!strcmp(unrelated[i],"我放好了") || !strcmp(unrelated[i],"放好了") ||
                !strcmp(unrelated[i],"再试一次") || !strcmp(unrelated[i],"重新试一次");
            bool expected=retry && (!strcmp(states[s],"failed") || !strcmp(states[s],"needs_clearer_view"));
            assert(p4_voice_tutor_start_allowed(unrelated[i],states[s],photo!=0)==expected);++combinations;
        }
    }
    assert(!p4_voice_tutor_start_allowed(NULL,"idle",false));
    assert(!p4_voice_tutor_start_allowed("看题",NULL,false));
    /* Bound applies to the entire original input, before stripping padding.
     * A long suffix must never be truncated into a valid command. */
    char boundary[386];memset(boundary,' ',sizeof(boundary));
    memcpy(boundary,"拍题",strlen("拍题"));boundary[384]=0;
    assert(p4_voice_tutor_request(boundary,false)==P4_TUTOR_REQUEST_FRESH);
    boundary[384]=' ';boundary[385]=0;
    assert(p4_voice_tutor_request(boundary,false)==P4_TUTOR_REQUEST_NONE);
    /* Confirmation uses the same normalization/boundary as acquisition.
     * Long padding used to bypass the old 100-byte confirmation matcher. */
    char padded_confirm[385];memset(padded_confirm,' ',sizeof(padded_confirm));
    memcpy(padded_confirm,"开始解题",strlen("开始解题"));padded_confirm[384]=0;
    for(unsigned s=0;s<sizeof(states)/sizeof(*states);++s)for(unsigned photo=0;photo<2;++photo)
        assert(!p4_voice_tutor_start_allowed(padded_confirm,states[s],photo!=0));
    char long_story[512];memset(long_story,'x',sizeof(long_story));
    memcpy(long_story,"讲故事",strlen("讲故事"));long_story[sizeof(long_story)-1]=0;
    assert(!p4_voice_tutor_start_allowed(long_story,"idle",false));
    char suffix[512];memset(suffix,'x',sizeof(suffix));
    memcpy(suffix,"拍题",strlen("拍题"));suffix[sizeof(suffix)-1]=0;
    assert(!p4_voice_tutor_start_allowed(suffix,"idle",false));
    printf("PASS acquisition grammar fresh=%u current=%u negative=%u state_combinations=%u\n",
        (unsigned)(sizeof(fresh)/sizeof(*fresh)),(unsigned)(sizeof(current)/sizeof(*current)),
        (unsigned)(sizeof(unrelated)/sizeof(*unrelated)),combinations);
}
static void direct_story_cases(void)
{
    const char *yes[]={"讲一个300字的故事。","讲一个故事","请给我讲一个三百字的睡前故事。",
        "讲个200字故事","能不能讲一则寓言故事？","我想听一个故事", "你能给我讲个故事吗",
        "麻烦编一篇大约500字左右的科幻故事", "写一个关于小兔子和乌龟的故事吧",
        "请讲一个关于取消拍题的故事。","说个有趣的故事给我听","讲故事"};
    const char *no[]={"不要讲故事","请不要讲一个故事","他说讲一个300字的故事",
        "“讲一个故事”是什么意思","\"讲一个故事\"","如果讲故事就拍照",
        "讲个故事然后重新拍照","先拍照再讲故事","请讲一个故事并取消拍题",
        "讲一个关于这道题的故事","用摄像头讲一个故事","讲一个关于照片的故事",
        "继续讲解这道题","讲第三步","请重新拍照只读第一题","是的就是这道题请开始解题",
        "12减3是多少","讲一个300的故事","讲个故事之前先重新拍照","",
        "讲一个关于小兔子的故事然后讲解题目"};
    for(unsigned i=0;i<sizeof(yes)/sizeof(*yes);++i){
        assert(p4_quick_story(yes[i]));assert(!p4_quick_capture(yes[i]));assert(!p4_quick_cancel(yes[i]));
    }
    for(unsigned i=0;i<sizeof(no)/sizeof(*no);++i)assert(!p4_quick_story(no[i]));
    assert(!p4_quick_story(NULL));
    char long_request[386];memset(long_request,'x',sizeof(long_request));long_request[385]=0;
    assert(!p4_quick_story(long_request));
}
int main(void)
{
    direct_story_cases();
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
    const char *confirm_yes[]={"请开始解题。","是的，就是这道题，请开始解题。",
        "是这道题，请开始解题。","对，就是这道题！","是的，开始解题"};
    const char *confirm_no[]={"请不要开始解题","开始解题了吗","如果是这道题请开始解题",
        "他说请开始解题","“请开始解题”","不是这道题请开始解题","请开始解题然后拍第二题",
        "是这道题但先不要解题","请开始解题是什么意思",""};
    for(unsigned i=0;i<sizeof(confirm_yes)/sizeof(*confirm_yes);++i)assert(p4_quick_confirm(confirm_yes[i]));
    for(unsigned i=0;i<sizeof(confirm_no)/sizeof(*confirm_no);++i)assert(!p4_quick_confirm(confirm_no[i]));
    assert(!p4_quick_confirm(NULL));
    /* Reconnect172 accepted this recorded 380 ms candidate, then the model
     * wrongly started a second capture. Confirmation cannot become start. */
    assert(!p4_voice_tutor_start_allowed("嗯。","idle",false));
    const char *acknowledgements[]={" 嗯嗯！ ","呃","啊？","哦。","好的","好吧","是的。","对，就是这道题！",""};
    for(unsigned i=0;i<sizeof(acknowledgements)/sizeof(*acknowledgements);++i)
        assert(!p4_voice_tutor_start_allowed(acknowledgements[i],"idle",false));
    assert(!p4_voice_tutor_start_allowed(NULL,"idle",false));
    assert(p4_quick_confirm("是的。")); /* Existing explicit confirmation is preserved. */
    assert(p4_voice_tutor_start_allowed("请重新拍照，只读第一题。","idle",false));
    assert(p4_voice_tutor_start_allowed("好的，请再拍一张题图。","idle",false));
    assert(p4_voice_tutor_start_allowed("帮我看一下镜头前这道题","idle",false));
    cancellation_authority_cases();
    acquisition_cases();
    return 0;
}
