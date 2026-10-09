#ifndef AGENT_SPEECH_INTENT_H
#define AGENT_SPEECH_INTENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Narrow lexical guard for an explicit, unfinished self-correction in final
 * ASR. False does NOT establish semantic completeness or a valid endpoint. */
bool agent_speech_unfinished_repair(const char *text);
/* Activity hint only: at least two Han characters or three Latin letters.
 * Input must already be valid UTF-8. This is not an endpoint or instruction. */
bool agent_speech_meaningful_partial(const char *text);
/* Narrow incomplete-argument hint; false does not prove semantic completion. */
bool agent_speech_pending_argument(const char *text);
/* Borrowed content after an explicit Mandarin/Cantonese remember prefix.
 * It can be empty or incomplete; callers still require full final admission. */
const char *agent_speech_remember_content(const char *text);
/* Mandarin/Cantonese explanatory question: a bounded sentence-pause hint,
 * never evidence that a question is complete or permission to act. */
bool agent_speech_phrase_pause(const char *text);
typedef enum { AGENT_GUESS_NONE,AGENT_GUESS_SELF,AGENT_GUESS_LIGHT,AGENT_GUESS_REMEMBER,
               AGENT_GUESS_THINK,AGENT_GUESS_ANSWER } agent_speech_guess_t;
enum { AGENT_SPEECH_ROUTE_HISTORY=1, AGENT_SPEECH_ROUTE_FULL=2 };
/* Existing fast-path routing policy; nonzero needs Agent tools/context. */
unsigned agent_speech_route(const char *text);
/* Partial nomination is permissive; final admission is a narrow grammar.
 * It never authorizes an action or claims general semantic understanding. */
agent_speech_guess_t agent_speech_guess(const char *text,bool final);
bool agent_speech_guess_reply(agent_speech_guess_t,const char *text);
/* Shape only: a bounded topic and a thinking receipt, never a factual answer
 * or action. Caller must also ground its characters in the nominated input. */
const char *agent_speech_thinking_topic(const char *text,size_t *bytes);
/* Complete unconditional lamp setter, including bounded explicit repairs;
 * never call on a preview. Leaves rgb untouched on rejection. */
bool agent_speech_light_literal(const char *text,uint8_t rgb[3]);
/* Speculative synthesis policy only: a literal lamp command or an incomplete
 * local spelling may wait for final ASR without generating an unused receipt.
 * No RGB is returned and true NEVER authorizes an effect or proves completion.
 * Unknown compound suffixes remain eligible for generic model preparation. */
bool agent_speech_light_local_preview(const char *text);
/* Internal literal vocabulary helper; the macro includes its empty terminator. */
bool agent_speech_has_words(const char *text,const char *nul_words);
#define AGENT_SPEECH_HAS(text,words) agent_speech_has_words(text,words "\0")
#endif
