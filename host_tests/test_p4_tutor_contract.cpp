#include "p4_tutor_contract.h"
#include <cstdlib>
#include <iostream>

using namespace p4_tutor;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(0)

static cJSON *reader(const char *selected="二(1)",const char *basis="explicit_user") {
    auto r=cJSON_Parse(R"JSON({"readable":true,"questions":[{"key":"二(1)","question":"求极限 lim(x->0+) x^a (ln x)^b，a>0，b>0。","requires_image":false},{"key":"二(2)","question":"计算另一道独立的极限 lim(x->0) sin(x)/x。","requires_image":false},{"key":"三","question":"求曲线围成的面积，已知图形与给定边界。","requires_image":true}],"reason":""})JSON");
    cJSON_AddStringToObject(r,"selected_key",selected);cJSON_AddStringToObject(r,"selection_basis",basis);return r;
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
int main() {selection_cases();scope_cases();lifecycle_cases();explicit_user_cases();explicit_user_priority_cases();std::cout << "PASS p4_tutor_contract " << checks << " checks\n";}
