#include "macro_record.hpp"
#include "macro_setting.hpp"
#include "macro_rename.hpp"
#include "focus.hpp"
#include "game.hpp"
#include <ultra.hpp>
#include <time.h>
#include "macro_sampler.hpp"


extern std::string g_recordMessage;

namespace {
    constexpr u64 STICK_PSEUDO_MASK = 0xFF0000ULL;  // 伪按键，必须过滤
    
    // 比较两帧是否相同（不比较时间戳）
    bool isSameFrame(const MacroFrameV2& a, const MacroFrameV2& b) {
        return a.keysHeld == b.keysHeld &&
               a.leftX == b.leftX && a.leftY == b.leftY &&
               a.rightX == b.rightX && a.rightY == b.rightY;
    }
}

// 录制用的 Frame
RecordingFrame::RecordingFrame() 
 : m_RecordingTimeText("REC  00:00")
{
}

void RecordingFrame::setRecordingTime(u64 elapsed_s) {
    snprintf(m_RecordingTimeText, sizeof(m_RecordingTimeText), "REC  %02lu:%02lu", elapsed_s / 60, elapsed_s % 60);
}

void RecordingFrame::draw(tsl::gfx::Renderer* renderer) {
    // 绘制全透明背景
    renderer->fillScreen(tsl::Color(0x0, 0x0, 0x0, 0x0));
    auto FrameWidthSize = tsl::cfg::FramebufferWidth - 60;
    auto FrameHightSize = 100;
    auto FrameX = 40;
    auto FrameY = 20;
    auto circleRadius = 12;
    auto circleX = FrameX + 25 + circleRadius;
    auto circleY = FrameY + FrameHightSize / 2;
    auto textFontSize = 50;
    auto textDim = renderer->getTextDimensions(m_RecordingTimeText, false, textFontSize);
    auto textX = circleX + circleRadius + 29;
    auto textY = (FrameY + FrameHightSize / 2 + textDim.second / 2) - 5;
    renderer->drawRoundedRect(FrameX, FrameY, FrameWidthSize, FrameHightSize, 10, tsl::Color(0x0, 0x0, 0x0, 0x8));
    renderer->drawStringWithColoredSections(
        m_RecordingTimeText,                // "REC  00:00"
        false,                              // 不使用等宽字体
        {"REC"},                            // 特殊符号列表
        textX, textY,                       // 坐标
        textFontSize,                       // 字体大小50
        tsl::Color(0xF, 0xF, 0xF, 0xF),     // 白色 - 默认颜色（用于时间部分"00:00"）
        tsl::Color(0xF, 0x5, 0x5, 0xF)      // 亮红色 - 特殊颜色（用于"REC"，和圆球同色）
    );
    renderer->drawCircle(circleX, circleY, circleRadius, true, tsl::Color(0xF, 0x5, 0x5, 0xF));
}

void RecordingFrame::layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) {
    // 设置边界
    this->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
}

// 录制时界面类
RecordingGui::RecordingGui()
 : m_startTime(armGetSystemTick())
{
    // 移动到屏幕右上角
    u16 centerX = (tsl::cfg::ScreenWidth - tsl::cfg::LayerWidth);
    tsl::gfx::Renderer::get().setLayerPos(centerX, 0);
    tsl::hlp::requestForeground(false);
    MacroSampler::Start();
}

tsl::elm::Element* RecordingGui::createUI() {
    m_frame = new RecordingFrame();
    return m_frame;
}

// 退出录制并返回主界面
void RecordingGui::exitRecording() {
    tsl::disableHiding = false;
    tsl::gfx::Renderer::get().setLayerPos(0, 0);
    tsl::hlp::requestForeground(true);
    tsl::goBack();  // 返回脚本主界面
}

void RecordingGui::update() {
    // 检查焦点，如果不在当前游戏，中断录制
    if (ult::currentForeground.load(std::memory_order_acquire)) tsl::hlp::requestForeground(false); 
    u64 elapsed_ms = armTicksToNs(armGetSystemTick() - m_startTime) / 1000000ULL;
    // 每100ms检查一次焦点
    if (elapsed_ms >= m_lastFocusCheckMs + 100) {
        m_lastFocusCheckMs = elapsed_ms;
        FocusState focusState = FocusMonitor::GetState(GameMonitor::getCurrentTitleId());
        if (focusState == FocusState::OutOfFocus) {
            g_recordMessage = "录制已中断";
            MacroSampler::Cancel();
            exitRecording();
            return;
        }
    }
    
    // 更新显示（每1000ms更新一次）
    if (elapsed_ms >= m_lastUpdatedSeconds + 1000) {
        m_lastUpdatedSeconds = elapsed_ms;
        m_frame->setRecordingTime(elapsed_ms / 1000);
    }
    
    // 超时检测
    if (elapsed_ms > 80000) {
        g_recordMessage = "录制超时";
        MacroSampler::Cancel();
        exitRecording();
    }
}

bool RecordingGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) {
    // 录制期间使用特斯拉快捷键结束录制
    const u64 combo = tsl::cfg::launchCombo;
    if (((keysHeld & combo) == combo) && (keysDown & combo)) {
        MacroSampler::Stop();
        u64 titleId = GameMonitor::getCurrentTitleId();
        MacroSampler::Save(titleId, combo);
        char filename[128];
        strcpy(filename, MacroSampler::GetFilePath());
        char gameName[64];
        GameMonitor::getTitleIdGameName(titleId, gameName);
        tsl::disableHiding = false;                                     // 恢复特斯拉区域触摸和快捷键hide的功能
        tsl::gfx::Renderer::get().setLayerPos(0, 0);                    // 恢复特斯拉区默认位置
        tsl::hlp::requestForeground(true);                              // 恢复特斯拉的输入焦点
        tsl::swapTo<MacroRenameGui>(SwapDepth(1), filename, gameName, true); // 录制成功，跳转到保存与命名界面
        return true;
    }

    // 录制期间不接受其他输入
    return true;
}

