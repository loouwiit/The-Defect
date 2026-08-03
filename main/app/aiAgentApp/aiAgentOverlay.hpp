#pragma once

#include "display/display.hpp"
#include "aiAgent/aiConfig.hpp"
#include "mutex/mutex.hpp"
#include <string>
#include <vector>
#include <functional>

/**
 * @brief AI 助手浮动 Overlay（lv_layer_sys）
 *
 * 驻留在 lv_layer_sys() 上的可拖拽浮动窗口，不参与 AppStack 导航。
 * 因此 display->getActiveApp() 始终返回底层真实 App，getAiContext() 工作正常。
 *
 * 唤出方式：
 * - 屏幕顶部下滑手势（由 gestureDetection Task 检测）
 * - 手柄 SELECT+START 组合键
 * - 桌面 AI 卡片点击
 *
 * 布局（400×280）：
 * ┌──────────────────────┐
 * │ ✕ AI 助手            │  标题栏（可拖拽）
 * ├──────────────────────┤
 * │ 对话记录 (4-5 行)    │  lv_textarea 只读
 * │ ─── 思考中... ───   │  loading 标签
 * ├──────────────────────┤
 * │ [输入..........] [▶] │  输入行
 * │  Q W E R T Y ...    │  LVGL 键盘
 * └──────────────────────┘
 */
class AiAgentOverlay {
public:
    static AiAgentOverlay& instance();

    /** @brief 初始化：在 lv_layer_sys() 上创建 UI，初始 hidden */
    bool start(Display* display);

    /** @brief 显示/隐藏/切换（须持 LVGL 锁） */
    void show();
    void hide();
    void toggle();
    bool isVisible() const { return m_visible; }

    /** @brief 显示/隐藏键盘（隐藏时聊天区获得更多空间） */
    void setKeyboardVisible(bool visible);

    /** @brief 线程安全入口 */
    void requestShow();
    void requestToggle();

    /** @brief 报告 Lua 游戏运行时错误（线程安全，供 LuaGameApp 调用）
     *  下次用户发消息时自动注入到 AI 上下文，促成 AI 自我修正 */
    static void reportLuaError(const std::string& err);

    /** @brief 清除 Lua 错误（generate_lua_game 成功时由 executor 调用） */
    static void clearLuaError();

    /** @brief 报告 read 动作读取的文件内容（线程安全，供 AiExecutor 调用）
     *  下次 AI 对话时注入到 system prompt */
    static void reportFileRead(const std::string& path, const std::string& content);

    /** @brief 报告上一轮执行反馈/错误（线程安全，供 AiExecutor/解析失败路径调用）
     *  下次 AI 对话时注入到 system prompt，促成 AI 自我修正 */
    static void reportAiError(const std::string& msg);

private:
    AiAgentOverlay() = default;
    ~AiAgentOverlay() = default;
    AiAgentOverlay(const AiAgentOverlay&) = delete;
    AiAgentOverlay& operator=(const AiAgentOverlay&) = delete;

    // ── LVGL 定时器（手势 + 手柄组合键检测，不拦截触摸事件） ──
    lv_timer_t* m_gestureTimer{};
    lv_point_t m_gestureStart{};    // 触摸起始点
    bool m_gestureTracking{};       // 是否正在跟踪
    bool m_gamepadComboPrev{};      // 手柄组合键边沿检测
    static constexpr int32_t SWIPE_THRESHOLD = 50;  // px
    static void gestureTimerCb(lv_timer_t* timer);

    // ── 手柄摇杆输入（LVGL keypad indev） ──
    lv_indev_t* m_gamepadIndev{};
    lv_group_t* m_gamepadGroup{};
    // 上一帧各玩家按键状态，用于边沿检测
    uint16_t m_prevButtons[4]{};
    static void gamepadReadCb(lv_indev_t* indev, lv_indev_data_t* data);

    // ── IME 输入法 ──
    lv_obj_t* m_pinyinIme{};
    lv_obj_t* m_candPanel{};

    // ── 输入法模式（拼音/五笔/英文），键盘图标按钮循环切换 ──
    enum class ImeMode { Pinyin, Wubi, English };
    ImeMode m_imeMode{ ImeMode::Pinyin };
    bool isChinese() const { return m_imeMode != ImeMode::English; }
    void setImeMode(ImeMode mode);

    // ── 依赖 ──
    Display* m_display{};

    // ── 可见性 ──
    bool m_visible{};
    bool m_started{};

    // ── LVGL 对象（建在 lv_layer_sys() 上） ──
    lv_obj_t* m_container{};    // 浮动窗根容器
    lv_obj_t* m_titleBar{};     // 标题栏（含关闭按钮）
    lv_obj_t* m_closeBtn{};     // 关闭按钮 ✕
    lv_obj_t* m_chatLog{};      // 对话记录 lv_label
    lv_obj_t* m_chatWrapper{};  // 聊天区滚动容器
    lv_obj_t* m_inputField{};   // 输入框
    lv_obj_t* m_sendBtn{};      // 发送按钮
    lv_obj_t* m_modeLabel{};    // 拼/五/EN 模式指示
    lv_obj_t* m_keyboard{};     // LVGL 键盘

    // ── 状态 ──
    AiConfig m_config;
    bool m_isWaiting{};
    int m_followUpCount{};
    static constexpr int MAX_FOLLOW_UP = 5;
    std::string m_lastUserInput;
    std::string m_lastSystemPrompt;
    std::string m_systemPromptTemplate;   // 含 {app_context}/{memory_context} 占位符的原始模板
    std::vector<std::string> m_chatHistory;

    // ── 对话上下文 ──
    static constexpr size_t MAX_HISTORY_TURNS = 8;   // 含 follow-up 合成轮，给多轮链留余量
    std::vector<std::pair<std::string, std::string>> m_history;  // (role, content)
    std::string m_memoryText;                                     // AI 长期记忆
    std::string m_promptCache;                                    // system prompt 模板缓存
    std::string m_pendingLuaError;                                // 待注入的 Lua 错误（m_pendingMutex 保护）
    std::string m_pendingFileRead;                                 // 待注入的读文件内容（m_pendingMutex 保护）
    std::string m_pendingAiError;                                 // 待注入的执行反馈/错误（m_pendingMutex 保护）
    Mutex m_pendingMutex;                                         // 保护 m_pendingLuaError / m_pendingFileRead / m_pendingAiError

    TickType_t m_nextActionTime{};
    TickType_t m_nextPageFlipTime{};
    static constexpr TickType_t ACTION_COOLDOWN = 300;
    static constexpr TickType_t PAGE_FLIP_COOLDOWN = 400;

    // ── UI 构建 ──
    void buildUi();

    // ── UI 操作 ──
    void addChatEntry(const std::string& role, const std::string& text);
    void setLoading(bool loading);

    // ── 发送逻辑 ──
    void onSend();
    void onAiResponse(bool success, const std::string& responseText, const std::string& errorMsg);

    // ── prompt 构建 ──
    std::string loadPromptTemplate();   // 加载 system prompt 模板（缓存，避免每次重读）
    std::string buildSystemPrompt(const std::string& templatePrompt,
                                  const std::string& appCtx,
                                  const std::string& memoryText,
                                  const std::string& luaErr,
                                  const std::string& fileRead,
                                  const std::string& aiErr);
    void sendFollowUpRound(const std::string& followUpMsg);   // 发送 follow-up 轮（nextRound/自愈共用）

    // ── IME 键盘图标按钮（循环切换 拼/五/EN） ──
    static void kbExtraBtnCb(lv_event_t* e);

    // ── LVGL 事件回调 ──
    static void onCloseBtnCb(lv_event_t* e);
    static void onSendBtnCb(lv_event_t* e);
    static void onKeyboardReadyCb(lv_event_t* e);
    static void onDragEndCb(lv_event_t* e);  // 拖拽边界约束

    // ── 内部数据 ──
    struct AiSendCtx {
        AiAgentOverlay* self;
        std::string text;
        std::string err;
        bool ok;
    };
};

/** @brief 默认 System Prompt（/root/system/ai_prompt.txt 不存在时的 fallback） */
extern const char* DEFAULT_SYSTEM_PROMPT;
