#pragma once
// Pure lesson contracts shared with host tests. No hardware or model calls.
#include "cJSON.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace p4_tutor {
inline const char *text(const cJSON *v,const char *key) {
    const auto item=cJSON_GetObjectItemCaseSensitive(v,key);
    return cJSON_IsString(item)?item->valuestring:nullptr;
}
inline bool bounded(const char *v,size_t maximum) {return v && *v && std::char_traits<char>::length(v)<=maximum;}
inline bool uint_equals(const cJSON *v,const char *key,unsigned expected) {
    const auto item=cJSON_GetObjectItemCaseSensitive(v,key);
    return cJSON_IsNumber(item) && item->valuedouble==double(expected);
}
inline bool exact_fields(const cJSON *v,std::initializer_list<const char *> names) {
    if(!cJSON_IsObject(v) || cJSON_GetArraySize(v)!=int(names.size()))return false;
    for(const auto name:names) {
        unsigned count=0;
        for(auto child=v->child;child;child=child->next)if(child->string && std::string(child->string)==name)++count;
        if(count!=1)return false;
    }
    return true;
}
// One acquired JPEG identity is retained across same-photo selection and solving.
struct PhotoSource {
    unsigned capture_id=0;
    uint64_t captured_at_ms=0;
    size_t jpeg_bytes=0;
    std::string kind="none",sha256;
    bool valid() const {return capture_id && jpeg_bytes && (kind=="camera" || kind=="fixture") && sha256.size()==64;}
};
enum class Acquisition { reject,fresh_camera,fresh_fixture,retained };
inline bool refine_current(const std::string &phase,unsigned current_id,unsigned current_revision,
                           unsigned requested_id,unsigned requested_revision,bool has_photo) {
    return (phase=="awaiting_confirmation" || phase=="needs_selection") && has_photo && current_id &&
        requested_id==current_id && requested_revision==current_revision;
}
inline Acquisition acquisition(const std::string &action,const cJSON *request,const std::string &phase,
                               unsigned id,unsigned revision,bool has_photo) {
    const bool fixture=cJSON_GetObjectItemCaseSensitive(request,"test_image_url")!=nullptr;
    // A new question never inherits a previous photo, even while confirmation is pending.
    if(action=="start")return fixture?Acquisition::fresh_fixture:Acquisition::fresh_camera;
    if(action!="refine" || fixture || !uint_equals(request,"question_id",id) ||
       !uint_equals(request,"revision",revision) || !refine_current(phase,id,revision,id,revision,has_photo))
        return Acquisition::reject;
    return Acquisition::retained;
}
struct PhotoWindow {size_t encoded_offset=0,encoded_count=0,skip=0,count=0;};
// Decode only the aligned base64 span, never allocate another complete JPEG.
inline bool photo_window(size_t total,unsigned offset,unsigned count,PhotoWindow &out) {
    if(!total || offset>=total || !count || count>1024)return false;
    PhotoWindow window;window.skip=offset%3;window.count=std::min<size_t>(count,total-offset);
    window.encoded_offset=size_t(offset/3)*4;window.encoded_count=(window.skip+window.count+2)/3*4;
    out=window;return true;
}
struct Question {std::string key,question;bool requires_image=false;};
struct Selection {std::vector<Question> questions;Question selected;std::vector<std::string> incomplete_keys;};
inline bool parse_selection(const cJSON *v,bool has_user_hint,Selection &out,std::string &reason) {
    Selection parsed;
    if(!cJSON_IsObject(v)) {reason="invalid_reader_envelope";return false;}
    const auto readable=cJSON_GetObjectItemCaseSensitive(v,"readable");
    if(!cJSON_IsBool(readable)) {reason="invalid_readability";return false;}
    // False never adopts any candidate text. An explicit cropped target can
    // still be diagnosed precisely without accepting its readable neighbours.
    if(cJSON_IsFalse(readable)) {
        const char *target=text(v,"target_status"),*basis=text(v,"selection_basis");
        const auto chosen=cJSON_GetObjectItemCaseSensitive(v,"selected_key");
        bool empty=cJSON_IsNull(chosen) || (cJSON_IsString(chosen) && !*chosen->valuestring);
        reason=has_user_hint && target && std::string(target)=="incomplete" && basis &&
            std::string(basis)=="target_incomplete" && empty?"target_question_incomplete":"question_not_readable";
        return false;
    }
    const auto all=cJSON_GetObjectItemCaseSensitive(v,"questions");int count=cJSON_GetArraySize(all);
    if(!cJSON_IsArray(all) || count<1 || count>12) {reason="invalid_question_candidates";return false;}
    std::vector<std::string> keys;
    for(int i=0;i<count;++i) {
        const auto entry=cJSON_GetArrayItem(all,i);const char *key=text(entry,"key"),*question=text(entry,"question");
        if(!bounded(key,64)) {reason="invalid_question_key";return false;}
        for(const auto &old:keys)if(old==key) {reason="duplicate_question_key";return false;}
        keys.push_back(key);
        const auto complete=cJSON_GetObjectItemCaseSensitive(entry,"complete");
        if(!cJSON_IsBool(complete)) {reason="missing_question_completeness";return false;}
        if(cJSON_IsFalse(complete)){parsed.incomplete_keys.push_back(key);continue;}
        if(!bounded(question,16000)) {reason="invalid_question_candidate";return false;}
        const auto image=cJSON_GetObjectItemCaseSensitive(entry,"requires_image");
        if(!cJSON_IsBool(image)) {reason="missing_question_image_requirement";return false;}
        parsed.questions.push_back({key,question,cJSON_IsTrue(image)!=0});
    }
    const auto chosen=cJSON_GetObjectItemCaseSensitive(v,"selected_key");
    // Providers use explicit null for an unanswered selection. Missing fields
    // and other types remain contract errors; null never selects a candidate.
    const char *key=cJSON_IsNull(chosen)?"":text(v,"selected_key");
    const char *basis=text(v,"selection_basis"),*target=text(v,"target_status");
    if(!key || !basis) {reason="missing_question_selection";return false;}
    if(!target || (std::string(target)!="unspecified" && std::string(target)!="matched" &&
       std::string(target)!="incomplete" && std::string(target)!="ambiguous")) {reason="invalid_target_status";return false;}
    if(std::string(target)=="incomplete") {
        if(!has_user_hint || *key || std::string(basis)!="target_incomplete") {reason="invalid_target_selection";return false;}
        reason="target_question_incomplete";return false;
    }
    if(*key)for(const auto &partial:parsed.incomplete_keys)if(partial==key) {reason="target_question_incomplete";return false;}
    if(parsed.questions.empty()) {reason="no_complete_question";return false;}
    if(!*key) {
        if(std::string(basis)!="needs_user_selection" || std::string(target)=="matched") {reason="invalid_selection_basis";return false;}
        out=std::move(parsed);reason="needs_user_selection";return true;
    }
    if(std::string(basis)=="only_question") {
        if(parsed.questions.size()!=1 || std::string(target)!="unspecified") {reason="ambiguous_question_selection";return false;}
    } else if(std::string(basis)!="explicit_user" || !has_user_hint || std::string(target)!="matched") {
        reason="unconfirmed_question_selection";return false;
    }
    for(const auto &candidate:parsed.questions)if(candidate.key==key)parsed.selected=candidate;
    if(parsed.selected.key.empty()) {reason="selected_question_not_found";return false;}
    out=std::move(parsed);reason.clear();return true;
}
enum class CaptureFailure { image_quality,internal };
inline CaptureFailure capture_failure(const std::string &reason) {
    for(const char *quality:{"blurred","moving","exposure_or_low_contrast","insufficient_edges"})
        if(reason==quality)return CaptureFailure::image_quality;
    // Timing, initialization, audio/preview contention, resource failures and
    // unknown reasons do not prove that the user moved or blurred the camera.
    return CaptureFailure::internal;
}
enum class ReaderKind { ready,unreadable,target_incomplete,truncated,json_error,protocol_error,selection_error };
inline const char *reader_kind_name(ReaderKind kind) {
    switch(kind) {
        case ReaderKind::ready:return "ready";
        case ReaderKind::unreadable:return "unreadable";
        case ReaderKind::target_incomplete:return "target_incomplete";
        case ReaderKind::truncated:return "truncated";
        case ReaderKind::json_error:return "json_error";
        case ReaderKind::selection_error:return "selection_error";
        default:return "protocol_error";
    }
}
struct ReaderResult {
    ReaderKind kind=ReaderKind::protocol_error;
    Selection selection;
    std::string reason,detail;
    bool retry_same_photo() const {return kind==ReaderKind::json_error || kind==ReaderKind::protocol_error || kind==ReaderKind::selection_error;}
    bool needs_target_view() const {return kind==ReaderKind::unreadable || kind==ReaderKind::target_incomplete;}
};
inline ReaderResult read_response(const char *answer,const std::string &finish,bool has_user_hint) {
    ReaderResult result;
    if(finish=="length") {result.kind=ReaderKind::truncated;result.reason="reader_truncated";return result;}
    auto parsed=answer?cJSON_ParseWithOpts(answer,nullptr,true):nullptr;
    if(!parsed) {result.kind=ReaderKind::json_error;result.reason="invalid_reader_json";return result;}
    if(const char *detail=text(parsed,"reason"))result.detail=detail;
    bool valid=parse_selection(parsed,has_user_hint,result.selection,result.reason);cJSON_Delete(parsed);
    if(valid)result.kind=ReaderKind::ready;
    else if(result.reason=="question_not_readable")result.kind=ReaderKind::unreadable;
    else if(result.reason=="target_question_incomplete" || result.reason=="no_complete_question")result.kind=ReaderKind::target_incomplete;
    else if(result.reason=="duplicate_question_key" || result.reason=="missing_question_selection" ||
            result.reason=="invalid_selection_basis" || result.reason=="ambiguous_question_selection" ||
            result.reason=="unconfirmed_question_selection" || result.reason=="selected_question_not_found" ||
            result.reason=="invalid_target_selection")result.kind=ReaderKind::selection_error;
    return result;
}
// A source identity, not an authentication primitive. IDs and revisions are checked too.
inline std::string source_hash(const Question &q) {
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned char c:q.key+"\n"+q.question+(q.requires_image?"\nimage":"\ntext")){hash^=c;hash*=UINT64_C(1099511628211);}
    char out[17];std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(hash));return out;
}
inline bool scoped_text(const cJSON *section,const Question &q,std::string &result) {
    const char *key=text(section,"key"),*value=text(section,"text");
    if(!exact_fields(section,{"key","text"}) || !key || q.key!=key || !bounded(value,24000))return false;
    result=value;
    for(const char *heading:{"【题目】","【思路】","【步骤","【答案】","【检验】"})if(result.find(heading)!=std::string::npos)return false;
    return true;
}
inline bool parse_solution(const cJSON *v,const Question &q,unsigned id,unsigned revision,
                           std::string &document,std::string &reason,const std::vector<Question> *candidates=nullptr) {
    if(!exact_fields(v,{"scope","overview","steps","answer","verification"})) {reason="invalid_solution_envelope";return false;}
    const auto scope=cJSON_GetObjectItemCaseSensitive(v,"scope");
    const char *key=text(scope,"key"),*hash=text(scope,"source_hash");
    const auto covered=cJSON_GetObjectItemCaseSensitive(scope,"covered_question_keys");
    const auto only=cJSON_GetArrayItem(covered,0);
    if(!exact_fields(scope,{"question_id","revision","key","source_hash","covered_question_keys"}) ||
       !uint_equals(scope,"question_id",id) || !uint_equals(scope,"revision",revision) ||
       !key || q.key!=key || !hash || source_hash(q)!=hash || !cJSON_IsArray(covered) ||
       cJSON_GetArraySize(covered)!=1 || !cJSON_IsString(only) || q.key!=only->valuestring) {
        reason="solution_scope_mismatch";return false;
    }
    std::string overview,answer,verification;
    if(!scoped_text(cJSON_GetObjectItemCaseSensitive(v,"overview"),q,overview) ||
       !scoped_text(cJSON_GetObjectItemCaseSensitive(v,"answer"),q,answer) ||
       !scoped_text(cJSON_GetObjectItemCaseSensitive(v,"verification"),q,verification)) {
        reason="invalid_solution_sections";return false;
    }
    const auto steps=cJSON_GetObjectItemCaseSensitive(v,"steps");int count=cJSON_GetArraySize(steps);
    if(!cJSON_IsArray(steps) || count<1 || count>128) {reason="invalid_solution_steps";return false;}
    std::string output="【题目】\n"+q.key+"\n"+q.question+"\n\n【思路】\n"+overview;
    for(int i=0;i<count;++i) {
        std::string body;
        if(!scoped_text(cJSON_GetArrayItem(steps,i),q,body)) {reason="solution_step_scope_mismatch";return false;}
        output+="\n\n【步骤"+std::to_string(i+1)+"】\n"+body;
        if(output.size()>128*1024) {reason="solution_too_large";return false;}
    }
    output+="\n\n【答案】\n"+answer+"\n\n【检验】\n"+verification;
    if(output.size()>128*1024) {reason="solution_too_large";return false;}
    if(candidates)for(const auto &other:*candidates) {
        if(other.key==q.key)continue;
        bool foreign_statement=other.question.size()>=24 && q.question.find(other.question)==std::string::npos && output.find(other.question)!=std::string::npos;
        bool foreign_key=other.key.size()>=4 && q.question.find(other.key)==std::string::npos && output.find(other.key)!=std::string::npos;
        if(foreign_statement || foreign_key) {reason="solution_contains_unselected_question";return false;}
    }
    document=std::move(output);reason.clear();return true;
}
enum class UserIntent { none,read,explain };
// Deliberately narrow whole-utterance grammar. Substring matching would treat
// negations, quotations, other topics and "先讲还是先看" as real choices.
inline UserIntent explicit_user_intent(const char *input) {
    if(!input || std::char_traits<char>::length(input)>256)return UserIntent::none;
    std::string utterance=input;
    for(const char *mark:{" ","\t","\r","\n","，","。","！","？","、","；",",",".","!","?",";"}) {
        size_t at=0,n=std::char_traits<char>::length(mark);
        while((at=utterance.find(mark,at))!=std::string::npos)utterance.erase(at,n);
    }
    for(unsigned i=0;i<6;++i) {
        bool removed=false;
        for(const char *prefix:{"好的","那么","麻烦","好","嗯","呃","哦","那","请"}) {
            size_t n=std::char_traits<char>::length(prefix);
            if(utterance.compare(0,n,prefix)==0){utterance.erase(0,n);removed=true;break;}
        }
        if(!removed)break;
    }
    for(unsigned i=0;i<3;++i) {
        bool removed=false;
        for(const char *suffix:{"可以吗","好吗","吧","呀","啊","呗","呢","吗"}) {
            size_t n=std::char_traits<char>::length(suffix);
            if(utterance.size()>=n && utterance.compare(utterance.size()-n,n,suffix)==0) {
                utterance.resize(utterance.size()-n);removed=true;break;
            }
        }
        if(!removed)break;
    }
    for(const char *phrase:{"先讲","你先讲","你讲","讲一下","你讲一下","讲一讲","你讲一讲",
        "先讲一下","你先讲一下","讲解一下","你讲解一下","讲讲","你讲讲","你给我讲一下","你先给我讲一下",
        "解题思路","讲解题思路","你讲解题思路","讲一下解题思路","你讲一下解题思路",
        "讲讲解题思路","你讲讲解题思路","给我讲一下解题思路","给我讲讲解题思路",
        "先讲解题思路","你先讲解题思路","先讲一下解题思路","你先讲一下解题思路",
        "讲一下整体思路","你讲一下整体思路","说一下解题思路","你说一下解题思路"})
        if(utterance==phrase)return UserIntent::explain;
    for(const char *phrase:{"先看","我先看","我先看看","我先看一下","先看一下","我先自己看",
        "我先自己看看","我自己先看","我自己看看","让我先看","让我先看一下","让我先看看",
        "我先读一下","我先读","先阅读","我先阅读","我想先看","我想先看一下",
        "先别讲我先看","先别讲我先看一下","先别讲我先看看","不用讲我先看","不用讲我先看一下",
        "不要讲我先看一下","你先别讲我先看一下","我先看一下先别讲","我先看不用讲","先不讲我先看一下"})
        if(utterance==phrase)return UserIntent::read;
    return UserIntent::none;
}
struct LessonState {
    unsigned question_id=0,revision=0,step=0,pending_step=0;
    std::string mode="none",pending_action;
    bool solving=false,choice_pending=false,pending_user_choice=false;
    bool matches(unsigned id,unsigned rev) const {return id==question_id && rev==revision;}
    void reset(unsigned id) {*this=LessonState{};question_id=id;}
    void begin_solve(bool retry=false) {solving=true;mode="solving";if(!retry){pending_action.clear();pending_step=0;pending_user_choice=false;}choice_pending=false;}
    bool choose(const std::string &action,unsigned id,unsigned rev,unsigned requested_step) {
        if(!matches(id,rev))return false;
        std::string next=action=="read" || action=="resume"?"reading":action=="explain"?"explaining":action=="done"?"done":"";
        if(next.empty())return false;
        // Tools can arrive after the current turn's transcription. They may
        // acknowledge its choice, but cannot replace explicit user intent.
        if(solving && pending_user_choice && (next!=pending_action ||
           (next=="explaining" && requested_step!=pending_step)))return false;
        choice_pending=false;
        if(solving){pending_action=next;pending_step=requested_step;}
        else {mode=next;step=requested_step;pending_action.clear();pending_step=0;}
        return true;
    }
    bool user_text(const char *input,unsigned id,unsigned rev) {
        if(!solving || !question_id || !matches(id,rev))return false;
        if(!input || std::string(input).find_first_not_of(" \t\r\n")==std::string::npos)return false;
        // A new real user turn releases the previous turn's guard. Unknown
        // wording is left to this turn's normal model/tool interpretation.
        pending_user_choice=false;
        auto intent=explicit_user_intent(input);
        bool chosen=intent!=UserIntent::none && choose(intent==UserIntent::explain?"explain":"read",id,rev,0);
        if(chosen)pending_user_choice=true;
        return chosen;
    }
    bool complete(unsigned id,unsigned previous_revision,bool recheck) {
        if(!solving || !matches(id,previous_revision))return false;
        ++revision;solving=false;pending_user_choice=false;
        mode=pending_action.empty()?(recheck?"explaining":"choice"):pending_action;
        step=pending_action.empty()?0:pending_step;
        choice_pending=mode=="choice";pending_action.clear();pending_step=0;return true;
    }
    void fail() {solving=false;mode="paused";choice_pending=false;}
};
} // namespace p4_tutor
