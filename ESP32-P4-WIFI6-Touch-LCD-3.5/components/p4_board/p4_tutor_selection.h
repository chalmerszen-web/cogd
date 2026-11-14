#pragma once
#include "p4_tutor_contract.h"

namespace p4_tutor {
enum class AnswerScope {none,count,answer};
inline AnswerScope answer_scope(const char *input) {
    if(!input)return AnswerScope::none;
    std::string value=input;
    if(value.find("第")!=std::string::npos && value.find("题")!=std::string::npos)return AnswerScope::none;
    if(value.find("不要告诉")!=std::string::npos || value.find("别说")!=std::string::npos ||
       value.find("如果")!=std::string::npos || value.find("“")!=std::string::npos)return AnswerScope::none;
    for(const char *term:{"几个步骤","多少个步骤","多少步骤","有几步","一共几步"})
        if(value.find(term)!=std::string::npos)return AnswerScope::count;
    if(value.find("最终答案")!=std::string::npos && (value.find("只")!=std::string::npos || value.find("直接")!=std::string::npos))return AnswerScope::answer;
    // An observed ASR result dropped the opening "请只告诉". Accept the
    // remaining explicit noun-phrase request, without swallowing a question,
    // quotation or explanation that merely mentions the same answer.
    std::string compact=value;
    for(const char *mark:{" ","\t","\n","\r","，","。","！",",",".","!"}) {
        size_t at;while((at=compact.find(mark))!=std::string::npos)
            compact.erase(at,std::char_traits<char>::length(mark));
    }
    if(compact=="我这道题的最终答案不要讲步骤" || compact=="这道题的最终答案不要讲步骤")return AnswerScope::answer;
    return AnswerScope::none;
}
struct SelectionRequest {unsigned ordinal=0;bool fresh=false;std::string mode="reading";};
inline SelectionRequest selection_request(const char *input) {
    SelectionRequest out;if(!input || std::char_traits<char>::length(input)>900)return out;
    std::string value=input;
    for(const char *mark:{" ","\t","\n","\r","，","。","！","？",",",".","!","?"}) {
        size_t at;while((at=value.find(mark))!=std::string::npos)value.erase(at,std::char_traits<char>::length(mark));
    }
    for(const char *v:{"不要","别","不想","不读","不解","不是第一","不是第二","不是第三","\"","“","”","假如","如果"})
        if(value.find(v)!=std::string::npos)return out;
    out.fresh=value.find("重拍")!=std::string::npos || value.find("重新拍")!=std::string::npos ||
        value.find("再拍")!=std::string::npos || value.find("换一张")!=std::string::npos;
    // A correction may mention the previous question before the actual command.
    size_t from=0;
    for(const char *v:{"我要","我只要","换成","改成","回到","返回","回去"}) {
        auto pos=value.rfind(v);if(pos!=std::string::npos)from=std::max(from,pos);
    }
    value=value.substr(from);
    if(value.find("大题")!=std::string::npos || value.find("小题")!=std::string::npos)return out;
    // Leave a compound follow-up to the normal tutor tool path. Selecting the
    // question alone must not swallow a requested step, comparison or reason.
    for(const char *v:{"步","为什么","难吗","对吗","比较","区别","答案对","重新算","重新核算"})
        if(value.find(v)!=std::string::npos)return out;
    unsigned matches=0;
    static const char *numbers[]={"一","二","三","四","五","六","七","八","九","十","十一","十二"};
    for(unsigned i=1;i<=12;++i)for(const auto &number:{std::string(numbers[i-1]),std::to_string(i)}) {
        for(const char *suffix:{"题","道题"}) {
            std::string token="第"+number+suffix;size_t at=0;
            while((at=value.find(token,at))!=std::string::npos){++matches;out.ordinal=i;at+=token.size();}
        }
    }
    if(matches!=1){out.ordinal=0;return out;}
    if(value.find("讲")!=std::string::npos || value.find("解")!=std::string::npos || value.find("做")!=std::string::npos)
        out.mode="explaining";
    return out;
}
inline bool retained_phase(const std::string &phase) {
    return phase=="awaiting_confirmation" || phase=="needs_selection" || phase=="solving" ||
        phase=="complete" || phase=="incomplete";
}
inline const Question *question_at(const std::vector<Question> &questions,
                                   const std::vector<std::string> &order,unsigned ordinal) {
    if(!ordinal || ordinal>order.size())return nullptr;
    for(const auto &q:questions)if(q.key==order[ordinal-1])return &q;
    return nullptr; // A cropped first question must not select its readable neighbour.
}
struct SavedLesson {Question question;LessonState lesson;std::string solution;};
} // namespace p4_tutor
