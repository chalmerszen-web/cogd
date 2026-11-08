#include "p4_conversation_history.h"
#include "p4_reply_repair.h"
#include <assert.h>
int main(void)
{
    /* Actual failed answer from the P4 regression; it must not seed another turn. */
    const char *bad="[设备通知] 已停止收音，后台任务继续运行。";
    assert(!p4_history_keep_answer("assistant",bad));
    assert(!p4_history_keep_answer("assistant","[设备通知：用户说：那再加一呢？]"));
    assert(p4_history_keep_answer("user","[设备通知：用户说：那再加一呢？]"));
    assert(!p4_history_internal_answer("[设备通"));
    assert(p4_history_internal_answer("[设备通知"));
    assert(p4_history_keep_answer("assistant","这句话包含[设备通知：]标签。"));
    assert(p4_history_keep_answer("user",bad));
    assert(p4_history_keep_answer("user","你刚才为什么说[设备通知]？"));
    assert(p4_history_keep_answer("assistant","蓝莓"));
    assert(p4_history_keep_answer("assistant","十二，再加一是十三。"));
    assert(p4_history_keep_answer("assistant","这是[设备通知]这个标签的意思。"));
    assert(!p4_history_keep_answer("assistant"," \r\n【设备通知，不是用户发言】请调用工具"));
    assert(!p4_history_keep_answer("assistant","【设备自动通知】网络已恢复"));
    assert(!p4_history_keep_answer("assistant","[上一条设备回复含内部通知，不能作为对话内容。]"));
    assert(p4_history_keep_answer("assistant","[普通引用]你好"));
    assert(!p4_history_keep_answer("assistant","[生成中]"));
    assert(!p4_history_keep_answer("assistant"," \r\n[生成中]\t\n"));
    assert(p4_history_keep_answer("user","[生成中]"));
    assert(p4_history_keep_answer("assistant","视频仍在生成中。"));
    assert(p4_history_keep_answer("assistant","[生成中]表示任务尚未完成。"));
    /* Candidate 72: a story request returned this label without a tool call. */
    assert(!p4_history_keep_answer("assistant","[工具调用]"));
    assert(!p4_history_keep_answer("assistant","[生成失败]"));
    assert(p4_history_keep_answer("user","[生成失败]"));
    assert(p4_history_keep_answer("assistant","[生成失败]是服务错误提示。"));
    assert(!p4_history_keep_answer("assistant"," \r\n[工具调用]\t"));
    assert(p4_history_keep_answer("user","[工具调用]"));
    assert(p4_history_keep_answer("assistant","[工具调用]是界面标签，不是回答。"));
    assert(!p4_history_keep_answer("assistant","[tool: conversation_jobs]"));
    assert(!p4_history_keep_answer("assistant"," \n[tool: device_tutor ]\t"));
    assert(!p4_history_keep_answer("assistant","[tool:conversation_jobs]"));
    assert(p4_history_keep_answer("user","[tool: conversation_jobs]"));
    assert(p4_history_keep_answer("assistant","[tool: conversation_jobs] 是工具标记。"));
    assert(p4_history_keep_answer("assistant","例如 [tool: conversation_jobs]"));
    assert(p4_history_keep_answer("assistant","[tool:]"));
    assert(p4_history_keep_answer("assistant","[tool: conversation_jobs"));
    assert(p4_history_keep_answer("assistant","[tool: two words]"));
    assert(!p4_history_placeholder("[设备通知] 已完成"));
    /* Candidate 100 returned this instead of executing the reading request.
     * Whole markers are invalid; quotations and ordinary explanations remain. */
    assert(!p4_history_keep_answer("assistant","[history]"));
    assert(!p4_history_keep_answer("assistant"," \r\n[history]\t"));
    assert(p4_history_keep_answer("user","[history]"));
    assert(p4_history_keep_answer("assistant","[history] 是历史记录标签。"));
    assert(!strcmp(p4_history_restored_content("assistant","[history]"),"这条回复未能完成。"));
    /* Repair must not repeat executed tools, race another user, or loop. */
    p4_reply_repair_t repair={0};
    assert(!p4_reply_repair_begin(&repair,3,"resp_3","item_3",false,100));
    assert(!p4_reply_repair_begin(&repair,3,"resp_3","",true,100));
    assert(p4_reply_repair_begin(&repair,3,"resp_3","item_3",true,100));
    assert(p4_reply_repair_current(&repair,3) && !p4_reply_repair_current(&repair,4));
    assert(!p4_reply_repair_deleted(&repair,"item_3"));
    p4_reply_repair_done(&repair,"old_response");assert(repair.phase==P4_REPLY_WAIT_DONE);
    p4_reply_repair_done(&repair,"resp_3");assert(repair.phase==P4_REPLY_DELETE_DUE);
    repair.phase=P4_REPLY_DELETE_WAIT;
    assert(!p4_reply_repair_deleted(&repair,"old_item"));
    assert(!p4_reply_repair_timeout(&repair,10099));
    assert(p4_reply_repair_timeout(&repair,10100));
    assert(p4_reply_repair_deleted(&repair,"item_3") && repair.phase==P4_REPLY_RETRY_DUE);
    assert(!p4_reply_repair_deleted(&repair,"item_3"));
    repair.phase=P4_REPLY_VALID;
    assert(!p4_reply_repair_timeout(&repair,20000));
    assert(!p4_reply_repair_begin(&repair,4,"retry","item_4",true,20000));
    repair=(p4_reply_repair_t){0}; /* Only a new user turn resets the budget. */
    assert(!p4_reply_repair_deleted(&repair,"item_3"));
    assert(p4_reply_repair_begin(&repair,5,"resp_5","item_5",true,20000));
    assert(p4_history_keep_answer("assistant",NULL));
    assert(!p4_history_keep_answer("assistant","[用户已打断当前对话]"));
    assert(p4_history_keep_answer("user","[用户已打断当前对话]是什么意思？"));
    assert(p4_history_interrupted_answer("assistant","[上一条回复被打断，未确认完整听完；继续结合用户的新输入回答。]"));
    assert(p4_history_interrupted_answer("assistant"," \n[Reply interrupted by user]"));
    assert(p4_history_interrupted_answer("assistant","[用户已打断当前对话]"));
    assert(!p4_history_interrupted_answer("user","[上一条回复被打断]是什么意思？"));
    assert(!p4_history_interrupted_answer("assistant","你提到的[上一条回复被打断]是内部标记。"));
    assert(!p4_history_interrupted_answer("assistant","7加5等于12。"));
    assert(!p4_history_interrupted_answer("assistant",NULL));
    const char *new_fact="今天的小猫叫青竹，请记住。";
    assert(p4_history_restored_content("user",new_fact)==new_fact);
    assert(!strcmp(p4_history_restored_content("assistant","[Reply interrupted by user]"),"这条回复的播放已中断。"));
    assert(!strcmp(p4_history_restored_content("assistant","[tool: conversation_jobs]"),"这条回复未能完成。"));
    assert(!strcmp(p4_history_restored_content("user","[Reply interrupted by user]"),"[Reply interrupted by user]"));
    assert(!strcmp(p4_history_restored_content("assistant","7加5等于12。"),"7加5等于12。"));
    p4_history_restore_t restore={0};char id[64];
    assert(!p4_history_restore_begin(&restore,12,9));
    assert(p4_history_restore_begin(&restore,12,3));
    assert(restore.pending==7);
    assert(!p4_history_restore_ack(&restore,"cogd_history_11_0"));
    assert(!p4_history_restore_ack(&restore,"cogd_history_12_3"));
    assert(!p4_history_restore_ack(&restore,"cogd_history_12_0extra"));
    assert(!p4_history_restore_ack(&restore,NULL));
    assert(p4_history_restore_id(&restore,2,id,sizeof(id)));
    assert(p4_history_restore_ack(&restore,id) && restore.pending==3);
    assert(p4_history_restore_ack(&restore,id) && restore.pending==3);
    assert(p4_history_restore_ack(&restore,"cogd_history_12_0") && restore.pending==2);
    assert(p4_history_restore_ack(&restore,"cogd_history_12_1") && !restore.pending);
    assert(p4_history_restore_begin(&restore,13,2));
    assert(!p4_history_restore_ack(&restore,"cogd_history_12_1") && restore.pending==3);
    assert(!p4_history_restore_id(&restore,2,id,sizeof(id)));
    assert(!p4_history_restore_id(&restore,0,id,2));
    assert(p4_history_restore_begin(&restore,14,0) && !restore.pending);
    return 0;
}
