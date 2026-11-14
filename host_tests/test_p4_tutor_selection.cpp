#include "p4_tutor_selection.h"
#include <cassert>
#include <iostream>
using namespace p4_tutor;
int main() {
    assert(answer_scope("总共有几个步骤？只说步骤数量，不要讲具体解法。") == AnswerScope::count);
    assert(answer_scope("请只告诉我这道题的最终答案，不要讲步骤。") == AnswerScope::answer);
    // Actual seed170 ASR omitted the opening request words. The remaining
    // noun phrase plus explicit no-steps constraint still scopes the answer.
    assert(answer_scope("我这道题的最终答案，不要讲步骤。") == AnswerScope::answer);
    assert(answer_scope("这道题的最终答案，不要讲步骤") == AnswerScope::answer);
    assert(answer_scope("我这道题的最终答案， 不要讲步骤！") == AnswerScope::answer);
    for(const char *s:{"我不想知道这道题的最终答案，不要讲步骤。",
        "我这道题的最终答案为什么是三，不要讲步骤。",
        "他说我这道题的最终答案，不要讲步骤。",
        "我这道题的最终答案，不要讲步骤，对吗？"})
        assert(answer_scope(s) == AnswerScope::none);
    assert(answer_scope("请解释第二步为什么能得到最终答案") == AnswerScope::none);
    assert(answer_scope("不要告诉我最终答案") == AnswerScope::none);
    for(const char *s:{"读一下，读一下第一道题。","我只要你读第一题啊","哦，行吧，你回去跟我解一下第一道题吧。",
        "不是，你这个是第二道题呀，我要你回去帮我重新解第一道题。"})assert(selection_request(s).ordinal==1);
    assert(selection_request("给我看第二题").ordinal==2);
    assert(selection_request("讲第三题").mode=="explaining");
    assert(selection_request("读第12题").ordinal==12);
    assert(selection_request("读第十二题").ordinal==12);
    for(const char *s:{"不要读第一题","我不是第一题","比较第一题和第二题","第二大题第一小题", "好了没", "给我看第二步", "如果我说第一题", "他说“读第一题”", "讲第一题第二步", "第一题难吗", "重新核算第一题"})
        assert(selection_request(s).ordinal==0);
    assert(selection_request("重新拍照读第一题").fresh);
    assert(!selection_request("不要重新拍第一题").fresh);
    std::vector<Question> candidates={{"(2)","1/2-2x=1/3",false},{"(3)","1.6*2-3x=40%",false}};
    std::vector<std::string> order={"(1)","(2)","(3)"};
    assert(!question_at(candidates,order,1));
    assert(question_at(candidates,order,2)->key=="(2)");
    assert(question_at(candidates,order,3)->question=="1.6*2-3x=40%");
    assert(!question_at(candidates,order,0) && !question_at(candidates,order,4));
    for(const char *s:{"awaiting_confirmation","needs_selection","solving","complete","incomplete"})assert(retained_phase(s));
    for(const char *s:{"idle","reading","cancelled","failed"})assert(!retained_phase(s));
    std::cout<<"PASS retained question selection regressions\n";
}
