#include "p4_tutor_contract.h"
#include <cstdlib>
#include <iostream>

using namespace p4_tutor;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(0)

static cJSON *reader(const char *selected="二(1)",const char *basis="explicit_user") {
    auto r=cJSON_Parse(R"JSON({"readable":true,"questions":[{"key":"二(1)","question":"求极限 lim(x->0+) x^a (ln x)^b，a>0，b>0。","requires_image":false,"complete":true},{"key":"二(2)","question":"计算另一道独立的极限 lim(x->0) sin(x)/x。","requires_image":false,"complete":true},{"key":"三","question":"求曲线围成的面积，已知图形与给定边界。","requires_image":true,"complete":true}],"reason":""})JSON");
    cJSON_AddStringToObject(r,"selected_key",selected);cJSON_AddStringToObject(r,"selection_basis",basis);cJSON_AddStringToObject(r,"target_status",std::string(basis)=="explicit_user"?"matched":"unspecified");return r;
}
static cJSON *section(const Question &q,const char *value) {
    auto r=cJSON_CreateObject();cJSON_AddStringToObject(r,"key",q.key.c_str());cJSON_AddStringToObject(r,"text",value);return r;
}
static cJSON *solution(const Question &q,unsigned id=7,unsigned rev=1) {
    auto r=cJSON_CreateObject(),scope=cJSON_AddObjectToObject(r,"scope");
    cJSON_AddNumberToObject(scope,"question_id",id);cJSON_AddNumberToObject(scope,"revision",rev);
    cJSON_AddStringToObject(scope,"key",q.key.c_str());cJSON_AddStringToObject(scope,"source_hash",source_hash(q).c_str());
    auto covered=cJSON_AddArrayToObject(scope,"covered_question_keys");cJSON_AddItemToArray(covered,cJSON_CreateString(q.key.c_str()));
    cJSON_AddItemToObject(r,"overview",section(q,"先核对定义域和参数，再考虑换元并检查等价关系。"));
    auto steps=cJSON_AddArrayToObject(r,"steps");cJSON_AddItemToArray(steps,section(q,"令 t=-ln x，故 x=e^(-t)。"));
    cJSON_AddItemToArray(steps,section(q,"按原题的实数定义域核验表达式是否有意义。"));
    cJSON_AddItemToObject(r,"answer",section(q,"条件不足时明确指出，不能擅自改题。"));
    cJSON_AddItemToObject(r,"verification",section(q,"逐项检查定义域、符号和极限方向。"));return r;
}
static void selection_cases() {
    Selection selected;std::string reason;auto r=reader();
    CHECK(parse_selection(r,true,selected,reason));CHECK(selected.questions.size()==3);
    CHECK(selected.selected.key=="二(1)");CHECK(!selected.selected.requires_image);
    CHECK(selected.selected.question=="求极限 lim(x->0+) x^a (ln x)^b，a>0，b>0。");
    CHECK(selected.selected.question.find("sin")==std::string::npos);cJSON_Delete(r);
    r=reader("","needs_user_selection");CHECK(parse_selection(r,false,selected,reason));CHECK(selected.selected.key.empty());cJSON_Delete(r);
    r=reader("二(1)","only_question");CHECK(!parse_selection(r,false,selected,reason));CHECK(reason=="ambiguous_question_selection");cJSON_Delete(r);
    r=reader();CHECK(!parse_selection(r,false,selected,reason));cJSON_Delete(r);
    r=reader("二(9)");CHECK(!parse_selection(r,true,selected,reason));cJSON_Delete(r);
    r=reader();auto entries=cJSON_GetObjectItemCaseSensitive(r,"questions");
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(entries,1),"key",cJSON_CreateString("二(1)"));
    CHECK(!parse_selection(r,true,selected,reason));CHECK(reason=="duplicate_question_key");cJSON_Delete(r);
}
static void scope_cases() {
    Selection selected;std::string reason,output;auto r=reader();CHECK(parse_selection(r,true,selected,reason));cJSON_Delete(r);
    const auto &q=selected.selected;
    r=solution(q);CHECK(parse_solution(r,q,7,1,output,reason,&selected.questions));
    CHECK(output.find(q.question)!=std::string::npos);CHECK(output.find("二(2)")==std::string::npos);
    CHECK(output.find("【步骤1】")!=std::string::npos && output.find("【步骤2】")!=std::string::npos);
    CHECK(!parse_solution(r,q,8,1,output,reason));CHECK(!parse_solution(r,q,7,2,output,reason));
    auto changed=q;changed.question+=" changed";CHECK(!parse_solution(r,changed,7,1,output,reason));cJSON_Delete(r);
    r=solution(q);auto covered=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"scope"),"covered_question_keys");
    cJSON_AddItemToArray(covered,cJSON_CreateString("二(2)"));CHECK(!parse_solution(r,q,7,1,output,reason));cJSON_Delete(r);
    r=solution(q);auto step=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r,"steps"),1);
    cJSON_ReplaceItemInObjectCaseSensitive(step,"key",cJSON_CreateString("二(2)"));CHECK(!parse_solution(r,q,7,1,output,reason));cJSON_Delete(r);
    r=solution(q);cJSON_ReplaceItemInObjectCaseSensitive(r,"overview",section(q,"【题目】\n另一道题\n【步骤1】\n混入章节"));
    CHECK(!parse_solution(r,q,7,1,output,reason));cJSON_Delete(r);
    r=solution(q);cJSON_AddItemToObject(r,"other_solution",section(q,"额外一道题"));CHECK(!parse_solution(r,q,7,1,output,reason));cJSON_Delete(r);
    r=solution(q);cJSON_ReplaceItemInObjectCaseSensitive(r,"verification",section(q,"另检查二(2)的答案。"));
    CHECK(!parse_solution(r,q,7,1,output,reason,&selected.questions));CHECK(reason=="solution_contains_unselected_question");cJSON_Delete(r);
    r=solution(q);cJSON_ReplaceItemInObjectCaseSensitive(r,"answer",section(q,selected.questions[1].question.c_str()));
    CHECK(!parse_solution(r,q,7,1,output,reason,&selected.questions));cJSON_Delete(r);
    r=solution(q);cJSON_DeleteItemFromObjectCaseSensitive(r,"verification");
    output="retained previous lesson";CHECK(!parse_solution(r,q,7,1,output,reason));CHECK(output=="retained previous lesson");cJSON_Delete(r);
    r=cJSON_Parse("{\"scope\":");CHECK(!parse_solution(r,q,7,1,output,reason));cJSON_Delete(r);
}
static cJSON *partial_reader() {
    return cJSON_Parse(R"JSON({"readable":true,"questions":[{"key":"例1(1)","complete":true,"question":"解方程：12-x=9。","requires_image":false},{"key":"例1(2)","complete":false,"question":"","requires_image":false},{"key":"例2(1)","complete":true,"question":"解方程：2.4+x=80%。","requires_image":false}],"selected_key":"例1(1)","selection_basis":"explicit_user","target_status":"matched","reason":""})JSON");
}
static ReaderResult classify(cJSON *r,const char *finish="stop",bool hint=true) {
    char *json=cJSON_PrintUnformatted(r);CHECK(json);auto result=read_response(json,finish,hint);cJSON_free(json);return result;
}
static void reader_failure_cases() {
    auto r=partial_reader();auto result=classify(r);
    CHECK(result.kind==ReaderKind::ready && !result.retry_same_photo() && !result.needs_target_view());
    CHECK(result.selection.questions.size()==2 && result.selection.incomplete_keys.size()==1);
    CHECK(result.selection.selected.key=="例1(1)" && result.selection.selected.question=="解方程：12-x=9。");
    CHECK(result.selection.questions[1].key=="例2(1)" && result.selection.questions[1].question=="解方程：2.4+x=80%。");
    CHECK(result.selection.selected.question.find("80%") == std::string::npos);
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_CreateString(""));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selection_basis",cJSON_CreateString("needs_user_selection"));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"target_status",cJSON_CreateString("unspecified"));
    result=classify(r,"stop",false);CHECK(result.kind==ReaderKind::ready && result.selection.selected.key.empty());
    cJSON_Delete(r);
    // A cropped explicit target cannot silently become its complete neighbour.
    r=partial_reader();cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_CreateString("例1(2)"));
    result=classify(r);CHECK(result.kind==ReaderKind::target_incomplete && result.selection.selected.key.empty());
    CHECK(!result.retry_same_photo() && result.needs_target_view());cJSON_Delete(r);
    r=partial_reader();cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_CreateString(""));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selection_basis",cJSON_CreateString("target_incomplete"));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"target_status",cJSON_CreateString("incomplete"));
    result=classify(r);CHECK(result.kind==ReaderKind::target_incomplete && result.selection.questions.empty());cJSON_Delete(r);
    // One complete independent question remains selectable beside a cut-off one.
    r=partial_reader();auto questions=cJSON_GetObjectItemCaseSensitive(r,"questions");cJSON_DeleteItemFromArray(questions,2);
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selection_basis",cJSON_CreateString("only_question"));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"target_status",cJSON_CreateString("unspecified"));
    result=classify(r,"stop",false);CHECK(result.kind==ReaderKind::ready && result.selection.selected.key=="例1(1)");cJSON_Delete(r);
    r=partial_reader();questions=cJSON_GetObjectItemCaseSensitive(r,"questions");
    for(int i:{0,2})cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(questions,i),"complete",cJSON_CreateFalse());
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_CreateString(""));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selection_basis",cJSON_CreateString("needs_user_selection"));
    cJSON_ReplaceItemInObjectCaseSensitive(r,"target_status",cJSON_CreateString("unspecified"));
    result=classify(r,"stop",false);CHECK(result.kind==ReaderKind::target_incomplete && result.reason=="no_complete_question");
    CHECK(!result.retry_same_photo() && result.needs_target_view());cJSON_Delete(r);
    // Hierarchical keys are distinct; collapsing them to (1) is a selection
    // contract error, never evidence of a blurry camera frame.
    r=partial_reader();questions=cJSON_GetObjectItemCaseSensitive(r,"questions");
    for(int i:{0,2})cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(questions,i),"key",cJSON_CreateString("(1)"));
    result=classify(r);CHECK(result.kind==ReaderKind::selection_error && result.reason=="duplicate_question_key");
    CHECK(result.retry_same_photo() && !result.needs_target_view());cJSON_Delete(r);
    r=partial_reader();questions=cJSON_GetObjectItemCaseSensitive(r,"questions");
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetArrayItem(questions,0),"complete");
    result=classify(r);CHECK(result.kind==ReaderKind::protocol_error && result.retry_same_photo() && !result.needs_target_view());cJSON_Delete(r);
    r=partial_reader();cJSON_DeleteItemFromObjectCaseSensitive(r,"target_status");
    result=classify(r);CHECK(result.kind==ReaderKind::protocol_error);cJSON_Delete(r);
    r=partial_reader();cJSON_ReplaceItemInObjectCaseSensitive(r,"readable",cJSON_CreateString("true"));
    result=classify(r);CHECK(result.kind==ReaderKind::protocol_error && result.reason=="invalid_readability");cJSON_Delete(r);
    r=partial_reader();result=classify(r,"length");CHECK(result.kind==ReaderKind::truncated && !result.needs_target_view() && !result.retry_same_photo());cJSON_Delete(r);
    for(const char *invalid:{"{\"readable\":true,","```json\n{}\n```","{} trailing", ""}) {
        result=read_response(invalid,"stop",true);CHECK(result.kind==ReaderKind::json_error);
        CHECK(result.retry_same_photo() && !result.needs_target_view());
    }
    for(const char *invalid:{"{}","[]","{\"readable\":null}"}) {
        result=read_response(invalid,"stop",true);CHECK(result.kind==ReaderKind::protocol_error && !result.needs_target_view());
    }
    // The preserved failing-image replay returned false with null selection
    // and no target_status. Its semantics must remain unreadable, not protocol.
    result=read_response(R"({"readable":false,"questions":[],"selected_key":null,"selection_basis":null,"reason":"图片极度模糊且失焦"})","stop",false);
    CHECK(result.kind==ReaderKind::unreadable && result.reason=="question_not_readable");
    CHECK(result.needs_target_view() && !result.retry_same_photo() && result.detail=="图片极度模糊且失焦");
    r=partial_reader();cJSON_ReplaceItemInObjectCaseSensitive(r,"readable",cJSON_CreateFalse());
    result=classify(r);CHECK(result.kind==ReaderKind::unreadable && result.selection.questions.empty());cJSON_Delete(r);
}
static void actual_reader_shapes_cases() {
    // Exact returned shapes from the three fixed-image provider runs. Keep
    // their nulls/false semantics; no live model or image dependency in tests.
    const char *clear=R"ACTUAL({"readable":true,"questions":[{"key":"例1(1)","complete":true,"question":"12 - x = 9","requires_image":false},{"key":"例1(2)","complete":false,"question":"","requires_image":false},{"key":"例2(1)","complete":true,"question":"2.4 + x = 80%","requires_image":false},{"key":"例2(2)","complete":false,"question":"","requires_image":false}],"selected_key":null,"selection_basis":"needs_user_selection","target_status":"ambiguous","reason":"图片中包含两个完整可读的子题（例1(1)和例2(1)）以及两个被裁切的不完整子题。用户未明确指定目标题目，且存在多个完整候选项，故目标状态为ambiguous，需用户进一步选择。"})ACTUAL";
    const char *cropped=R"ACTUAL({"readable":false,"questions":[{"key":"例1(1)","complete":true,"question":"12 - x = 9","requires_image":false},{"key":"例1(2)","complete":false,"question":"","requires_image":false},{"key":"例2(1)","complete":true,"question":"2.4 + x = 80%","requires_image":false},{"key":"例2(2)","complete":false,"question":"","requires_image":false}],"selected_key":"","selection_basis":"target_incomplete","target_status":"incomplete","reason":"用户指定的目标题目“例1(2)”在图片右侧被裁切，仅可见“(2) 7 -”，后续算式内容缺失，无法辨认完整题干。"})ACTUAL";
    const char *blurred=R"ACTUAL({"readable":false,"questions":[],"selected_key":null,"selection_basis":null,"target_status":null,"reason":"图片整体严重失焦且模糊，所有文字内容均无法辨认，无法识别任何完整的独立子题或题干信息。"})ACTUAL";
    auto result=read_response(clear,"stop",false);
    CHECK(result.kind==ReaderKind::ready && result.reason=="needs_user_selection");
    CHECK(result.selection.selected.key.empty() && result.selection.questions.size()==2 && result.selection.incomplete_keys.size()==2);
    CHECK(result.selection.questions[0].key=="例1(1)" && result.selection.questions[0].question=="12 - x = 9");
    CHECK(result.selection.questions[1].key=="例2(1)" && result.selection.questions[1].question=="2.4 + x = 80%");
    auto r=cJSON_Parse(clear);cJSON_DeleteItemFromObjectCaseSensitive(r,"selected_key");
    result=classify(r,"stop",false);CHECK(result.kind==ReaderKind::selection_error && result.selection.selected.key.empty());cJSON_Delete(r);
    for(const char *type:{"true","false","7","[]","{}"}) {
        r=cJSON_Parse(clear);cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_Parse(type));
        result=classify(r,"stop",false);CHECK(result.kind==ReaderKind::selection_error && result.selection.selected.key.empty());cJSON_Delete(r);
    }
    r=cJSON_Parse(clear);cJSON_ReplaceItemInObjectCaseSensitive(r,"target_status",cJSON_CreateString("matched"));
    result=classify(r);CHECK(result.kind==ReaderKind::selection_error && result.selection.selected.key.empty());cJSON_Delete(r);
    result=read_response(cropped,"stop",true);
    CHECK(result.kind==ReaderKind::target_incomplete && result.reason=="target_question_incomplete");
    CHECK(result.selection.questions.empty() && result.selection.selected.key.empty() && !result.retry_same_photo());
    result=read_response(cropped,"stop",false);CHECK(result.kind==ReaderKind::unreadable && result.selection.questions.empty());
    r=cJSON_Parse(cropped);cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_CreateNull());
    result=classify(r);CHECK(result.kind==ReaderKind::target_incomplete && result.selection.questions.empty());
    cJSON_ReplaceItemInObjectCaseSensitive(r,"selected_key",cJSON_CreateString("例1(1)"));
    result=classify(r);CHECK(result.kind==ReaderKind::unreadable && result.selection.questions.empty());cJSON_Delete(r);
    result=read_response(blurred,"stop",false);CHECK(result.kind==ReaderKind::unreadable && result.selection.questions.empty());
    result=read_response(blurred,"stop",true);CHECK(result.kind==ReaderKind::unreadable && result.selection.questions.empty());
}
static void capture_failure_cases() {
    for(const char *reason:{"blurred","moving","exposure_or_low_contrast","insufficient_edges"})
        CHECK(capture_failure(reason)==CaptureFailure::image_quality);
    for(const char *reason:{"candidate_stale","sample_gap","camera_initializing","camera_initializing_timeout",
        "no_frames","audio_busy","preview_unavailable","preview_allocation_failed","capture_allocation_failed",
        "warming_up","image_acquisition_failed","capture_deadline","unknown","","blurred_then_timeout"})
        CHECK(capture_failure(reason)==CaptureFailure::internal);
}
static void lifecycle_cases() {
    LessonState s;s.reset(7);s.begin_solve();
    CHECK(s.choose("explain",7,0,0));CHECK(s.pending_action=="explaining");
    CHECK(!s.choose("read",7,1,0));CHECK(s.pending_action=="explaining");
    CHECK(s.complete(7,0,false));CHECK(s.mode=="explaining" && s.revision==1 && !s.choice_pending);
    CHECK(!s.complete(7,0,false));CHECK(!s.choose("done",6,1,0));CHECK(s.mode=="explaining");
    CHECK(s.choose("read",7,1,1));CHECK(s.mode=="reading");
    s.begin_solve();CHECK(s.choose("explain",7,1,2));CHECK(s.choose("read",7,1,0));
    CHECK(s.complete(7,1,true));CHECK(s.mode=="reading" && s.step==0 && s.revision==2);
    s.reset(8);s.begin_solve();CHECK(s.complete(8,0,false));CHECK(s.mode=="choice" && s.choice_pending);
    CHECK(s.choose("explain",8,1,0));CHECK(!s.choice_pending);
    s.reset(9);s.begin_solve();CHECK(s.choose("explain",9,0,2));s.fail();
    CHECK(s.mode=="paused" && s.revision==0 && s.pending_action=="explaining");
    s.begin_solve(true);CHECK(s.complete(9,0,false));CHECK(s.mode=="explaining" && s.step==2 && !s.choice_pending);
    s.reset(10);s.begin_solve();CHECK(!s.complete(9,0,false));CHECK(s.solving && s.revision==0);
    CHECK(s.choose("done",10,0,0));CHECK(s.complete(10,0,false));CHECK(s.mode=="done" && !s.choice_pending);
}
static void explicit_user_cases() {
    for(const char *value:{"先讲","你讲一下吧","你讲一下解题思路吧。","解题思路","请你先讲一下好吗？",
        "好的，那你先讲吧。","呃，你讲一讲呗。","你先给我讲一下吧","请给我讲讲解题思路", " 你 讲 一 下 吧 \n"})
        CHECK(explicit_user_intent(value)==UserIntent::explain);
    for(const char *value:{"先看","我先看一下","我先看一下啊。","好的好的，我先看一下啊。",
        "那让我先看看吧","我先自己看吧","先别讲，我先看一下。","不用讲，我先看一下。",
        "我先看一下，先别讲。","请让我先看一下好吗？"})
        CHECK(explicit_user_intent(value)==UserIntent::read);
    for(const char *value:{"","在吗","是这道题","第二大题第一小题","确认","重新解这道题","好的",
        "你讲一下天气吧","你讲一下洛必达的历史吧","解题思路是什么意思","你讲完了吗","你讲一下了吗",
        "不要讲","不要你讲一下吧","你先别讲","我不想先看","不是让你讲一下","不，你讲一下吧",
        "先讲还是先看","你先讲，我先看一下","先看然后再讲","先讲或者先看都行",
        "他说你讲一下吧","用户说你讲一下吧","\"你讲一下吧\"","“你讲一下吧”",
        "如果我说你讲一下吧","等一下再讲","你给我看一下屏幕","先看天气再说", "不好，你先讲"})
        CHECK(explicit_user_intent(value)==UserIntent::none);
    CHECK(explicit_user_intent(nullptr)==UserIntent::none);
    std::string long_text(257,'x');CHECK(explicit_user_intent(long_text.c_str())==UserIntent::none);
    LessonState s;s.reset(11);
    CHECK(!s.user_text("你讲一下吧",11,0));CHECK(s.mode=="none" && s.pending_action.empty());
    s.begin_solve();CHECK(s.user_text("你讲一下吧",11,0));
    CHECK(s.pending_action=="explaining" && s.pending_step==0 && s.revision==0 && s.question_id==11);
    CHECK(!s.user_text("我先看一下",10,0));CHECK(!s.user_text("我先看一下",11,1));
    CHECK(s.pending_action=="explaining");
    CHECK(!s.user_text("你讲一下天气吧",11,0));CHECK(s.pending_action=="explaining");
    CHECK(s.user_text("我先看一下",11,0));CHECK(s.pending_action=="reading");
    CHECK(!s.user_text("先讲还是先看",11,0));CHECK(s.pending_action=="reading");
    CHECK(s.user_text("你讲一下解题思路吧",11,0));CHECK(s.complete(11,0,false));
    CHECK(s.mode=="explaining" && s.step==0 && s.revision==1 && !s.choice_pending);
    CHECK(!s.user_text("我先看一下",11,1));CHECK(s.mode=="explaining");
    s.reset(12);s.begin_solve();CHECK(s.user_text("我先看一下",12,0));CHECK(s.complete(12,0,false));
    CHECK(s.mode=="reading" && !s.choice_pending);
}
static void explicit_user_priority_cases() {
    LessonState s;s.reset(13);s.begin_solve();
    CHECK(s.user_text("你讲一下吧",13,0));CHECK(s.pending_user_choice);
    CHECK(!s.choose("read",13,0,0));CHECK(!s.choose("resume",13,0,0));CHECK(!s.choose("done",13,0,0));
    CHECK(!s.choose("explain",13,0,2));CHECK(s.pending_action=="explaining" && s.pending_step==0);
    CHECK(s.choose("explain",13,0,0));CHECK(s.pending_user_choice);
    CHECK(!s.user_text(nullptr,13,0));CHECK(!s.user_text("",13,0));CHECK(!s.user_text(" \t\r\n",13,0));
    CHECK(!s.user_text("我先看一下",12,0));CHECK(!s.user_text("我先看一下",13,1));CHECK(s.pending_user_choice);
    CHECK(!s.choose("read",13,0,0));
    CHECK(s.user_text("我先看一下",13,0));CHECK(s.pending_action=="reading" && s.pending_user_choice);
    CHECK(!s.choose("explain",13,0,0));CHECK(!s.choose("done",13,0,0));
    CHECK(s.choose("resume",13,0,0));
    CHECK(!s.user_text("请换一种说明方式",13,0));CHECK(!s.pending_user_choice && s.pending_action=="reading");
    CHECK(s.choose("explain",13,0,1));CHECK(s.pending_step==1);
    // If the tool beat ASR, the later explicit transcription still wins.
    CHECK(s.choose("read",13,0,0));CHECK(s.user_text("你讲一下吧",13,0));
    CHECK(s.pending_action=="explaining" && s.pending_user_choice);
    s.fail();CHECK(s.pending_user_choice);s.begin_solve(true);
    CHECK(s.pending_user_choice && !s.choose("read",13,0,0));
    CHECK(s.complete(13,0,false));CHECK(s.mode=="explaining" && !s.pending_user_choice);
    CHECK(s.choose("read",13,1,0));
    s.reset(14);s.begin_solve();CHECK(s.user_text("我先看一下",14,0));
    s.reset(15);CHECK(!s.pending_user_choice && s.pending_action.empty());s.begin_solve();CHECK(s.choose("done",15,0,0));
    s.reset(16);s.begin_solve();CHECK(s.user_text("你讲一下吧",16,0));
    s.fail();s.begin_solve(false);CHECK(!s.pending_user_choice && s.pending_action.empty());CHECK(s.choose("read",16,0,0));
}
static void acquisition_cases() {
    auto r=cJSON_Parse("{\"action\":\"start\"}");
    // A fresh physical question B must never be rewritten as a query/refinement
    // of retained A, regardless of A's confirmation or completion state.
    for(const char *phase:{"awaiting_confirmation","needs_selection","complete","incomplete","reading","solving","cancelled"}) {
        CHECK(acquisition("start",r,phase,41,0,true)==Acquisition::fresh_camera);
        CHECK(acquisition("start",r,phase,41,0,false)==Acquisition::fresh_camera);
    }
    cJSON_AddStringToObject(r,"test_image_url","https://test.invalid/explicit-usb-fixture.jpg");
    CHECK(acquisition("start",r,"awaiting_confirmation",41,0,true)==Acquisition::fresh_fixture);
    CHECK(acquisition("refine",r,"awaiting_confirmation",41,0,true)==Acquisition::reject);cJSON_Delete(r);
    r=cJSON_Parse("{\"action\":\"refine\",\"question_id\":41,\"revision\":0}");
    CHECK(acquisition("refine",r,"awaiting_confirmation",41,0,true)==Acquisition::retained);
    CHECK(acquisition("refine",r,"needs_selection",41,0,true)==Acquisition::retained);
    CHECK(acquisition("refine",r,"awaiting_confirmation",42,0,true)==Acquisition::reject);
    CHECK(acquisition("refine",r,"awaiting_confirmation",41,1,true)==Acquisition::reject);
    CHECK(acquisition("refine",r,"awaiting_confirmation",41,0,false)==Acquisition::reject);
    for(const char *phase:{"reading","solving","complete","cancelled","needs_clearer_view"})
        CHECK(acquisition("refine",r,phase,41,0,true)==Acquisition::reject);
    cJSON_DeleteItemFromObjectCaseSensitive(r,"revision");
    CHECK(acquisition("refine",r,"awaiting_confirmation",41,0,true)==Acquisition::reject);
    cJSON_AddNumberToObject(r,"revision",0);cJSON_DeleteItemFromObjectCaseSensitive(r,"question_id");
    CHECK(acquisition("refine",r,"awaiting_confirmation",41,0,true)==Acquisition::reject);cJSON_Delete(r);
    // Revalidation at worker admission rejects a request captured before a
    // cancel/new lesson, even if the initial call had valid IDs.
    CHECK(refine_current("awaiting_confirmation",41,0,41,0,true));
    CHECK(!refine_current("cancelled",41,0,41,0,true));
    CHECK(!refine_current("awaiting_confirmation",42,0,41,0,true));
    CHECK(!refine_current("awaiting_confirmation",41,1,41,0,true));
    CHECK(!refine_current("awaiting_confirmation",0,0,0,0,true));
    PhotoSource source{12,123456,4097,"fixture",std::string(64,'a')};CHECK(source.valid());
    auto same_photo=source;CHECK(same_photo.capture_id==12 && same_photo.kind=="fixture" && same_photo.sha256==source.sha256);
    source={};CHECK(!source.valid() && source.capture_id==0 && source.kind=="none" && source.sha256.empty());
    source={13,234567,2048,"camera",std::string(64,'b')};CHECK(source.valid() && source.capture_id!=same_photo.capture_id);
    source.kind="https://test.invalid/photo.jpg";CHECK(!source.valid());
    source.kind="camera";source.sha256.clear();CHECK(!source.valid());
}
static void photo_window_cases() {
    PhotoWindow w;
    CHECK(photo_window(4097,0,1024,w));CHECK(w.encoded_offset==0 && w.encoded_count==1368 && w.skip==0 && w.count==1024);
    CHECK(photo_window(4097,1024,1024,w));CHECK(w.encoded_offset==1364 && w.encoded_count==1368 && w.skip==1 && w.count==1024);
    CHECK(photo_window(4097,2048,1024,w));CHECK(w.encoded_offset==2728 && w.encoded_count==1368 && w.skip==2 && w.count==1024);
    CHECK(photo_window(4097,4096,1024,w));CHECK(w.encoded_offset==5460 && w.encoded_count==4 && w.skip==1 && w.count==1);
    auto previous=w;
    CHECK(!photo_window(4097,4097,1,w));CHECK(!photo_window(4097,0,0,w));CHECK(!photo_window(4097,0,1025,w));
    CHECK(!photo_window(0,0,1,w));CHECK(!photo_window(4097,UINT32_MAX,1024,w));
    CHECK(w.count==previous.count && w.encoded_offset==previous.encoded_offset);
    // Exercise every byte alignment and final padding; no requested raw byte
    // can fall outside the bounded decoded span used by the USB exporter.
    for(unsigned total=1;total<=33;++total)for(unsigned offset=0;offset<total;++offset) {
        CHECK(photo_window(total,offset,1024,w));
        CHECK(w.count==total-offset && w.encoded_offset+w.encoded_count==((total+2)/3)*4);
        CHECK(w.skip+w.count<=w.encoded_count/4*3 && w.encoded_count<=1368);
    }
}
int main() {acquisition_cases();photo_window_cases();selection_cases();scope_cases();reader_failure_cases();actual_reader_shapes_cases();capture_failure_cases();lifecycle_cases();explicit_user_cases();explicit_user_priority_cases();std::cout << "PASS p4_tutor_contract " << checks << " checks\n";}
