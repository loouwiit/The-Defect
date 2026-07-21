#pragma once

#include "display/display.hpp"
#include "aiAgent/aiConfig.hpp"
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
    lv_obj_t* m_keyboard{};     // LVGL 键盘

    // ── 状态 ──
    AiConfig m_config;
    bool m_isWaiting{};
    int m_followUpCount{};
    static constexpr int MAX_FOLLOW_UP = 3;
    std::string m_lastUserInput;
    std::string m_lastAiContent;
    std::string m_lastSystemPrompt;
    std::string m_systemPromptTemplate;   // 含 {app_context}/{memory_context} 占位符的原始模板
    std::vector<std::string> m_chatHistory;

    // ── 对话上下文 ──
    static constexpr size_t MAX_HISTORY_TURNS = 5;
    std::vector<std::pair<std::string, std::string>> m_history;  // (role, content)
    std::string m_memoryText;                                     // AI 长期记忆

    TickType_t m_nextActionTime{};
    static constexpr TickType_t ACTION_COOLDOWN = 300;

    // ── UI 构建 ──
    void buildUi();

    // ── UI 操作 ──
    void addChatEntry(const std::string& role, const std::string& text);
    void setLoading(bool loading);
    void updateContextHint();

    // ── 发送逻辑 ──
    void onSend();
    void onAiResponse(bool success, const std::string& responseText, const std::string& errorMsg);

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
