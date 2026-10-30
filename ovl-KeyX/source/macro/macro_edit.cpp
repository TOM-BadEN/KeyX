#include "macro_edit.hpp"
#include "macro_data.hpp"
#include "hiddata.hpp"
#include "refresh.hpp"
#include "ipc.hpp"
#include "macro_view.hpp"
#include "i18n.hpp"
#include <ultra.hpp>


namespace {
    static constexpr s32 TIMELINE_HEIGHT = 100;       // 时间轴区域高度
    static constexpr s32 ITEM_HEIGHT = 70;            // 每个动作项的高度
    static constexpr u32 REPEAT_DELAY = 20;           // 首次触发后延迟帧数
    static constexpr u32 REPEAT_RATE = 8;             // 重复触发间隔帧数

    // 摇杆方向图标
    const char* StickDirIcons[] = {"", "", "", "", "", "", "", "", ""};
    const char* getStickDirIcon(StickDir dir) { return StickDirIcons[static_cast<int>(dir)]; }
    
    // 生成动作显示文本
    std::string getActionText(const Action& action) {
        std::string text;
        if (action.stickL != StickDir::None) text = text + "" + getStickDirIcon(action.stickL);
        if (action.stickR != StickDir::None) text = text + (!text.empty() ? "+" : "") + "" + getStickDirIcon(action.stickR);
        if (action.buttons != 0) text = text + (!text.empty() ? "+" : "") + HidHelper::getCombinedIcons(action.buttons);
        if (text.empty()) text = i18n("无动作");
        return text;
    }
    
    // 计算闪烁进度 (0.0~1.0)
    double calcBlinkProgress() {
        u64 ns = armTicksToNs(armGetSystemTick());
        return (std::cos(2.0 * 3.14159265358979323846 * std::fmod(ns * 0.000000001 - 0.25, 1.0)) + 1.0) * 0.5;
    }
    
    // 计算闪烁颜色（在两个颜色之间插值）
    tsl::Color calcBlinkColor(tsl::Color c1, tsl::Color c2, double progress) {
        return tsl::Color(
            static_cast<u8>(c2.r + (c1.r - c2.r) * progress),
            static_cast<u8>(c2.g + (c1.g - c2.g) * progress),
            static_cast<u8>(c2.b + (c1.b - c2.b) * progress), 0xF);
    }
    
    // 按键编辑模式的按键布局
    constexpr const u64 EDIT_BTN_MASKS[16] = {
        BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L, BTN_R, BTN_ZL, BTN_ZR,
        BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT, BTN_START, BTN_STICKL, BTN_STICKR
    };
    
    // 菜单项标识枚举
    enum class MenuId {
        // 一级菜单
        Move, Insert, Delete, Duration, Buttons, Stick,
        // 移动子菜单
        MoveUp, MoveDown,
        // 插入子菜单
        InsertBefore, InsertAfter, CopyBefore, CopyAfter,
        // 删除子菜单
        DeleteAction, ResetAction,
        // 摇杆子菜单
        StickModify, StickDeleteAll, StickDeleteL, StickDeleteR,
    };
    
    // 菜单项定义
    struct MenuDef {
        MenuId id;
        const char* icon;
        const char* title;
        const char* tip;
        const MenuDef* children;
        int childCount;
    };
    
    // 子菜单定义
    constexpr MenuDef INSERT_SUB[] = {
        {MenuId::InsertBefore, "", "向前插入", "按  插入无动作", nullptr, 0},
        {MenuId::InsertAfter, "", "向后插入", "按  插入无动作", nullptr, 0},
        {MenuId::CopyBefore, "", "向前复制", "按  复制动作", nullptr, 0},
        {MenuId::CopyAfter, "", "向后复制", "按  复制动作", nullptr, 0},
    };
    
    constexpr MenuDef DELETE_SUB[] = {
        {MenuId::DeleteAction, "", "删除动作", "按  删除整个动作", nullptr, 0},
        {MenuId::ResetAction, "", "重置动作", "按  重置动作为无动作", nullptr, 0},
    };
    
    constexpr MenuDef STICK_SUB[] = {
        {MenuId::StickModify, "", "修改为虚拟方向", "这会丢失摇杆精度  选中  保存  返回", nullptr, 0},
        {MenuId::StickDeleteAll, "", "删除所有摇杆", "按  删除所有摇杆输入", nullptr, 0},
        {MenuId::StickDeleteL, "", "删除左摇杆", "按  删除左摇杆输入", nullptr, 0},
        {MenuId::StickDeleteR, "", "删除右摇杆", "按  删除右摇杆输入", nullptr, 0},
    };
    
    constexpr MenuDef MOVE_SUB[] = {
        {MenuId::MoveUp, "", "向前移动", "按  与前一个动作交换", nullptr, 0},
        {MenuId::MoveDown, "", "向后移动", "按  与后一个动作交换", nullptr, 0},
    };
    
    // 一级菜单
    constexpr MenuDef ROOT_MENU[] = {
        {MenuId::Move, "", "移动动作", "选择移动方式", MOVE_SUB, 2},
        {MenuId::Insert, "", "插入新动作", "选择插入方式", INSERT_SUB, 4},
        {MenuId::Delete, "", "删除或重置", "选择删除还是重置动作", DELETE_SUB, 2},
        {MenuId::Duration, "", "修改持续时间", "/ 切换位 / 调整  保存  取消", nullptr, 0},
        {MenuId::Buttons, "", "修改触发按钮", " 选中/取消选中  保存  返回", nullptr, 0},
        {MenuId::Stick, "", "修改或删除摇杆", "修改摇杆方向会丢失摇杆精度", STICK_SUB, 4},
    };
    constexpr int ROOT_MENU_COUNT = 6;
    
    // 获取菜单项的 id
    constexpr MenuId getMenuId(int menuLevel, int parentIndex, int index) {
        if (menuLevel == 0) return ROOT_MENU[index].id;
        return ROOT_MENU[parentIndex].children[index].id;
    }
}

MacroEditGui::MacroEditGui(const char* gameName, bool isRecord) 
 : m_gameName(gameName)
 , m_isRecord(isRecord)
{
    
    MacroData::loadFrameAndBasicInfo();
    MacroData::parseActions();
    char bakPath[128];
    snprintf(bakPath, sizeof(bakPath), "%s.bak", MacroData::getFilePath());
    m_bakMacro = ult::isFile(bakPath);
}

MacroEditGui::~MacroEditGui() {
    MacroData::undoCleanup();
    
}

tsl::elm::Element* MacroEditGui::createUI() {
    const auto& info = MacroData::getBasicInfo();
    auto frame = new tsl::elm::HeaderOverlayFrame(97);
    frame->setHeader(new tsl::elm::CustomDrawer([this, &info](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h) {
        // 标题（菜单模式下显示菜单项名称）
        if (m_menuMode) {
            const MenuDef* items = (m_menuLevel == 0) ? ROOT_MENU : ROOT_MENU[m_parentIndex].children;
            MenuId id = getMenuId(m_menuLevel, m_parentIndex, m_menuIndex);
            bool isStickWarning = (id == MenuId::Stick || id == MenuId::StickModify);
            tsl::Color titleColor = isStickWarning ? tsl::Color(0xF, 0x5, 0x5, 0xF) : tsl::defaultOverlayColor;
            r->drawString(items[m_menuIndex].title, false, 20, 50, 32, r->a(titleColor));
            r->drawString(items[m_menuIndex].tip, false, 20, 73, 15, r->a(tsl::style::color::ColorDescription));
        } else {
            r->drawString(m_gameName, false, 20, 50, 32, r->a(tsl::defaultOverlayColor));
            // 宏名字
            std::string fileName = info.fileName;
            auto [fnW, fnH] = r->getTextDimensions(fileName, false, 15);
            r->drawString(fileName, false, 20, 73, 15, r->a(tsl::style::color::ColorDescription));
            s32 offsetX = 20 + fnW;
            // 分界线
            auto [sepW, sepH] = r->getTextDimensions("", false, 15);
            r->drawString("", false, offsetX, 73, 15, tsl::style::color::ColorDescription);
            offsetX += sepW;
            // 恢复备份
            tsl::Color bakColor = m_bakMacro ? r->a(tsl::onTextColor) : tsl::style::color::ColorDescription;
            std::string bakText = " 恢复备份";
            auto [bakW, bakH] = r->getTextDimensions(bakText, false, 15);
            r->drawString(bakText, false, offsetX, 73, 15, bakColor);
            offsetX += bakW;
            // 分界线
            r->drawString("", false, offsetX, 73, 15, tsl::style::color::ColorDescription);
            offsetX += sepW;
            // 撤销修改
            tsl::Color undoColor = MacroData::canUndo() ? r->a(tsl::onTextColor) : tsl::style::color::ColorDescription;
            r->drawString(" 撤销修改", false, offsetX, 73, 15, undoColor);
        }
        const char* btnText = m_selectMode ? "  修改" : "  保存";
        r->drawString(btnText, false, 270, 693, 23, r->a(tsl::style::color::ColorText));
    }));
    
    auto drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h) {
        double progress = calcBlinkProgress();
        if (m_buttonEditMode) drawButtonEditArea(r, x, y, w, progress);
        else if (m_durationEditMode) drawDurationEditArea(r, x, y, w, progress);
        else if (m_stickEditMode) drawStickEditArea(r, x, y, w, progress);
        else if (m_menuMode) drawMenuArea(r, x, y, w, progress);
        else drawTimelineArea(r, x, y, w, progress);
        drawActionList(r, x, y, w, h, progress);
    });
    
    frame->setContent(drawer);
    return frame;
}

bool MacroEditGui::hasVirtualMergedStick() const {
    auto& actions = MacroData::getActions();
    for (s32 i = m_menuSelStart; i <= m_menuSelEnd; i++) {
        if (actions[i].stickMergedVirtual) return true;
    }
    return false;
}

bool MacroEditGui::hasStickL() const {
    auto& actions = MacroData::getActions();
    for (s32 i = m_menuSelStart; i <= m_menuSelEnd; i++) {
        if (actions[i].stickL != StickDir::None) return true;
    }
    return false;
}

bool MacroEditGui::hasStickR() const {
    auto& actions = MacroData::getActions();
    for (s32 i = m_menuSelStart; i <= m_menuSelEnd; i++) {
        if (actions[i].stickR != StickDir::None) return true;
    }
    return false;
}

bool MacroEditGui::isMenuItemEnabled(int index) const {
    bool isMultiSelect = (m_menuSelStart != m_menuSelEnd);
    MenuId id = getMenuId(m_menuLevel, m_parentIndex, index);
    switch (id) {
        case MenuId::Buttons:
            // 按键菜单：多选直接禁用，单选有虚拟合并摇杆禁用
            return !isMultiSelect && !hasVirtualMergedStick();
        case MenuId::Duration:
            // 有虚拟合并摇杆时禁用时长菜单
            return !hasVirtualMergedStick();
        case MenuId::StickModify:
            // 摇杆子菜单：多选时禁用修改摇杆
            return !isMultiSelect;
        case MenuId::StickDeleteAll:
            // 没有摇杆数据时禁用删除摇杆菜单
            return hasStickL() || hasStickR();
        case MenuId::StickDeleteL:
            return hasStickL();
        case MenuId::StickDeleteR:
            return hasStickR();
        case MenuId::MoveUp:
            return m_menuSelStart > 0;
        case MenuId::MoveDown: {
            auto& actions = MacroData::getActions();
            return m_menuSelEnd < (s32)actions.size() - 1;
        }
        default:
            return true;
    }
}

void MacroEditGui::drawActionList(tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h, double progress) {
    auto& actions = MacroData::getActions();
    
    // 顶部分隔线
    r->drawRect(x, y + 100 - 1, w, 1, r->a(tsl::separatorColor));
    
    s32 listY = y + 100 + 5;
    s32 listH = h - 100 - 73;
    m_listHeight = listH;
    s32 bottomY = y + h - 73;
    
    for (size_t i = 0; i < actions.size(); i++) {
        s32 itemY = listY + i * ITEM_HEIGHT - m_scrollOffset;
        if (itemY >= listY && itemY < bottomY) {
            r->drawRect(x + 4, itemY + ITEM_HEIGHT - 1, w - 8, 1, r->a(tsl::separatorColor));
            
            if ((s32)i == m_selectedIndex) {
                tsl::Color hlColor = m_selectMode 
                    ? calcBlinkColor(tsl::Color(0xF,0xD,0x0,0xF), tsl::Color(0x8,0x6,0x0,0xF), progress)
                    : calcBlinkColor(tsl::highlightColor1, tsl::highlightColor2, progress);
                r->drawBorderedRoundedRect(x, itemY, w + 4, ITEM_HEIGHT, 5, 5, r->a(hlColor));
            }
            
            std::string text = getActionText(actions[i]);
            std::string duration = std::to_string(actions[i].duration) + "ms";
            std::string indexStr = std::to_string(i + 1) + ". ";
            
            // 判断是否在多选范围内
            bool inSelectRange = false;
            if (m_selectMode && m_selectAnchor >= 0) {
                s32 selStart = std::min(m_selectAnchor, m_selectedIndex);
                s32 selEnd = std::max(m_selectAnchor, m_selectedIndex);
                inSelectRange = ((s32)i >= selStart && (s32)i <= selEnd);
            }
            
            // 序号颜色：多选范围内黄色，否则白色
            tsl::Color indexColor = inSelectRange ? tsl::Color(0xF, 0xD, 0x0, 0xF) : tsl::defaultTextColor;
            
            // 动作内容颜色（多选不影响）
            tsl::Color textColor = tsl::defaultTextColor;
            if (actions[i].stickMergedVirtual) {
                textColor = tsl::Color(0xF, 0x5, 0x5, 0xF);  // 虚拟合并：红色
            } else if (actions[i].modified) {
                textColor = tsl::Color(0xF, 0xA, 0xC, 0xF);  // 已修改：粉色
            }
            
            // 获取序号宽度
            auto [indexW, indexH] = r->getTextDimensions(indexStr.c_str(), false, 23);
            auto [durW, durH] = r->getTextDimensions(duration.c_str(), false, 20);
            
            // 截断动作文本
            s32 maxTextWidth = w - 19 - indexW - durW - 25;
            std::string truncatedText = r->limitStringLength(text, false, 23, maxTextWidth);
            
            // 分别绘制序号和动作内容
            r->drawString(indexStr.c_str(), false, x + 19, itemY + 45, 23, r->a(indexColor));
            r->drawString(truncatedText.c_str(), false, x + 19 + indexW, itemY + 45, 23, r->a(textColor));
            r->drawString(duration.c_str(), false, x + w - 15 - durW, itemY + 45, 20, r->a(tsl::onTextColor));
        }
    }
}

void MacroEditGui::drawTimelineArea(tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, double progress) {
    auto& actions = MacroData::getActions();
    float totalDuration = (float)MacroData::getBasicInfo().durationMs / 1000.0f;
    float pixelPerSec = (float)(w - 20) / totalDuration;
    s32 barH = 25, barY = y + 50, tickY = y + 27;
    
    // 时间刻度
    s32 tickInterval = (totalDuration > 20) ? 5 : (totalDuration > 10) ? 2 : 1;
    for (s32 t = 0; t <= (s32)totalDuration; t += tickInterval) {
        s32 tickX = x + 10 + (s32)(t * pixelPerSec);
        r->drawRect(tickX, tickY + 5, 1, 10, r->a(tsl::Color(0x8, 0x8, 0x8, 0xF)));
        char buf[16]; snprintf(buf, sizeof(buf), "%ds", t);
        r->drawString(buf, false, tickX - 5, tickY, 12, r->a(tsl::Color(0xA, 0xA, 0xA, 0xF)));
    }
    
    // 选择模式高亮色
    tsl::Color timelineHighlight = m_selectMode 
        ? calcBlinkColor(tsl::Color(0xF,0xD,0x0,0xF), tsl::Color(0x8,0x6,0x0,0xF), progress) 
        : calcBlinkColor(tsl::highlightColor1, tsl::highlightColor2, progress);
    
    // 选择范围
    s32 selStart = m_selectedIndex, selEnd = m_selectedIndex;
    if (m_selectMode && m_selectAnchor >= 0) {
        selStart = std::min(m_selectAnchor, m_selectedIndex);
        selEnd = std::max(m_selectAnchor, m_selectedIndex);
    }
    
    // 动作色块
    float currentTime = 0;
    s32 rangeStartX = -1, rangeEndX = -1;
    for (size_t i = 0; i < actions.size(); i++) {
        float dur = (float)actions[i].duration / 1000.0f;
        s32 blockX = x + 10 + (s32)(currentTime * pixelPerSec);
        s32 nextBlockX = x + 10 + (s32)((currentTime + dur) * pixelPerSec);
        s32 blockW = std::max(1, nextBlockX - blockX);
        bool inRange = ((s32)i >= selStart && (s32)i <= selEnd);
        s32 drawY = inRange ? barY - 3 : barY;
        s32 drawH = inRange ? barH + 6 : barH;
        bool hasStick = (actions[i].stickL != StickDir::None || actions[i].stickR != StickDir::None);
        bool hasButtons = (actions[i].buttons != 0);
        tsl::Color blockColor = actions[i].modified ? tsl::Color(0xF, 0xA, 0xC, 0xF)              // 粉色：已修改
                              : (hasButtons && !hasStick) ? tsl::Color(0x2, 0xA, 0x2, 0xF)       // 绿色：纯按键
                              : actions[i].stickMergedVirtual ? tsl::Color(0xF, 0x5, 0x5, 0xF)   // 红色：虚拟合并摇杆
                              : hasStick ? tsl::Color(0xF, 0xA, 0x3, 0xF)                        // 橙色：非虚拟合并摇杆（含按键+摇杆）
                              : tsl::Color(0x8, 0x8, 0x8, 0xF);                                  // 灰色：无动作
        r->drawRect(blockX, drawY, blockW, drawH, r->a(blockColor));
        if ((s32)i == selStart) rangeStartX = blockX;
        if ((s32)i == selEnd) rangeEndX = blockX + blockW;
        currentTime += dur;
    }
    
    // 选中框
    if (rangeStartX >= 0 && rangeEndX >= 0) {
        s32 boxX = rangeStartX - 2, boxW = rangeEndX - rangeStartX + 4, boxY = barY - 3, boxH = barH + 6;
        r->drawRect(boxX, boxY, boxW, 2, r->a(timelineHighlight));
        r->drawRect(boxX, boxY + boxH, boxW, 2, r->a(timelineHighlight));
        r->drawRect(boxX, boxY, 2, boxH, r->a(timelineHighlight));
        r->drawRect(rangeEndX, boxY, 2, boxH, r->a(timelineHighlight));
    }
}

void MacroEditGui::drawMenuArea(tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, double progress) {
    tsl::Color highlightColor = calcBlinkColor(tsl::highlightColor1, tsl::highlightColor2, progress);
    
    // 根据层级获取当前菜单
    const MenuDef* items = (m_menuLevel == 0) ? ROOT_MENU : ROOT_MENU[m_parentIndex].children;
    int count = (m_menuLevel == 0) ? ROOT_MENU_COUNT : ROOT_MENU[m_parentIndex].childCount;
    
    // 始终显示6格
    s32 itemW = w / 6;
    for (int i = 0; i < 6; i++) {
        s32 itemX = x + i * itemW;
        // 只绘制有内容的格子
        if (i < count) {
            bool enabled = isMenuItemEnabled(i);
            tsl::Color iconColor = enabled ? tsl::defaultTextColor : tsl::Color(0x5, 0x5, 0x5, 0xF);
            auto [iconW, iconH] = r->getTextDimensions(items[i].icon, false, 30);
            r->drawString(items[i].icon, false, itemX + (itemW - iconW) / 2, y + 58, 30, r->a(iconColor));
            if (i == m_menuIndex) r->drawBorderedRoundedRect(itemX, y + 7, itemW + 5, 73, 5, 5, r->a(highlightColor));
        }
        // 分隔线（除了最后一格）
        if (i < 5) r->drawRect(itemX + itemW, y + 22, 1, 56, r->a(tsl::separatorColor));
    }
}

void MacroEditGui::drawDurationEditArea(tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, double progress) {
    tsl::Color highlightColor = calcBlinkColor(tsl::highlightColor1, tsl::highlightColor2, progress);
    auto& actions = MacroData::getActions();
    s32 currentActionMs = actions[m_menuSelStart].duration;
    s32 totalMs = MacroData::getBasicInfo().durationMs;
    s32 maxEditMs = 80000 - (totalMs - currentActionMs);
    bool isOverLimit = m_editingDuration > maxEditMs;
    s32 digits[5] = { m_editingDuration/10000%10, m_editingDuration/1000%10, m_editingDuration/100%10, m_editingDuration/10%10, m_editingDuration%10 };
    s32 digitW = 30, gap = 8;
    s32 totalW = digitW * 5 + gap * 5 + 30;
    s32 startX = x + (w - totalW) / 2;
    tsl::Color errorColor = tsl::Color(0xF, 0x2, 0x2, 0xF);
    tsl::Color errorHighlight = calcBlinkColor(tsl::Color(0xF,0x4,0x4,0xF), tsl::Color(0x8,0x1,0x1,0xF), progress);
    for (int i = 0; i < 5; i++) {
        char buf[2] = {(char)('0' + digits[i]), '\0'};
        tsl::Color color = isOverLimit ? ((i == m_editDigitIndex) ? errorHighlight : errorColor) 
                                       : ((i == m_editDigitIndex) ? highlightColor : tsl::defaultTextColor);
        r->drawString(buf, false, startX + i * (digitW + gap), y + 60, 36, r->a(color));
    }
    r->drawString("ms", false, startX + 5 * (digitW + gap), y + 60, 28, r->a(isOverLimit ? errorColor : tsl::defaultTextColor));
    
    if (isOverLimit) {
        char maxBuf[32];
        snprintf(maxBuf, sizeof(maxBuf), "Max: %dms", maxEditMs);
        auto [maxW, maxH] = r->drawString(maxBuf, false, 0, 0, 14, tsl::Color(0,0,0,0));
        r->drawString(maxBuf, false, x + w - 15 - maxW, y + 90, 14, r->a(errorColor));
    }
}

void MacroEditGui::drawButtonEditArea(tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, double progress) {
    tsl::Color highlightColor = calcBlinkColor(tsl::highlightColor1, tsl::highlightColor2, progress);
    s32 cellW = w / 8;
    s32 rowH = 40;
    tsl::Color staticHighlight = tsl::Color(0x0, 0xF, 0xF, 0xF);
    for (int i = 0; i < 16; i++) {
        s32 cellX = x + (i % 8) * cellW;
        s32 cellY = y + 15 + (i / 8) * rowH;
        const char* icon = HidHelper::getIconByMask(EDIT_BTN_MASKS[i]);
        bool isSelected = (m_editingButtons & EDIT_BTN_MASKS[i]) != 0;
        tsl::Color iconColor = isSelected ? staticHighlight : tsl::defaultTextColor;
        auto [iconW, iconH] = r->drawString(icon, false, 0, 0, 24, tsl::Color(0,0,0,0));
        r->drawString(icon, false, cellX + (cellW - iconW) / 2, cellY + 28, 24, r->a(iconColor));
        if (i == m_buttonEditIndex) {
            r->drawBorderedRoundedRect(cellX + 3, cellY + 3, cellW - 3, rowH - 10, 3, 2, r->a(highlightColor));
        }
    }
}

bool MacroEditGui::handleButtonEditInput(u64 keysDown) {
    if (keysDown & HidNpadButton_AnyLeft) {
        m_buttonEditIndex = (m_buttonEditIndex % 8 == 0) ? m_buttonEditIndex + 7 : m_buttonEditIndex - 1;
        return true;
    }
    if (keysDown & HidNpadButton_AnyRight) {
        m_buttonEditIndex = (m_buttonEditIndex % 8 == 7) ? m_buttonEditIndex - 7 : m_buttonEditIndex + 1;
        return true;
    }
    if (keysDown & HidNpadButton_AnyUp) {
        m_buttonEditIndex = (m_buttonEditIndex + 8) % 16;
        return true;
    }
    if (keysDown & HidNpadButton_AnyDown) {
        m_buttonEditIndex = (m_buttonEditIndex + 8) % 16;
        return true;
    }
    if (keysDown & HidNpadButton_A) {
        m_editingButtons ^= EDIT_BTN_MASKS[m_buttonEditIndex];
        return true;
    }
    if (keysDown & HidNpadButton_Plus) {
        MacroData::setActionButtons(m_menuSelStart, m_editingButtons);
        m_buttonEditMode = false;
        m_selectMode = false;
        m_menuMode = false;
        m_menuLevel = 0;
        m_parentIndex = -1;
        return true;
    }
    if (keysDown & HidNpadButton_B) {
        m_buttonEditMode = false;
        return true;
    }
    return true;
}

void MacroEditGui::drawStickEditArea(tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, double progress) {
    tsl::Color highlightColor = calcBlinkColor(tsl::highlightColor1, tsl::highlightColor2, progress);
    s32 cellW = w / 9;  // 9列：1图标 + 8方向
    s32 rowH = 40;
    tsl::Color staticHighlight = tsl::Color(0x0, 0xF, 0xF, 0xF);
    
    // 绘制左摇杆行（行0）和右摇杆行（行1）
    for (int row = 0; row < 2; row++) {
        s32 cellY = y + 15 + row * rowH;
        
        // 第0列：摇杆图标（不可选）
        const char* stickIcon = (row == 0) ? "" : "";
        auto [sW, sH] = r->drawString(stickIcon, false, 0, 0, 24, tsl::Color(0,0,0,0));
        r->drawString(stickIcon, false, x + (cellW - sW) / 2, cellY + 28, 24, r->a(tsl::defaultTextColor));
        
        // 第1-8列：8个方向（可选，跳过None）
        for (int col = 0; col < 8; col++) {
            int i = row * 8 + col;  // 0-7=左摇杆, 8-15=右摇杆
            s32 cellX = x + (col + 1) * cellW;  // 从第1列开始
            
            StickDir dir = static_cast<StickDir>(col + 1);  // 跳过None(0)，从Up(1)开始
            const char* icon = getStickDirIcon(dir);
            
            // 判断是否被选中
            bool isSelected = (row == 0) ? (m_editingStickL == dir) : (m_editingStickR == dir);
            tsl::Color iconColor = isSelected ? staticHighlight : tsl::defaultTextColor;
            
            auto [iconW, iconH] = r->drawString(icon, false, 0, 0, 24, tsl::Color(0,0,0,0));
            r->drawString(icon, false, cellX + (cellW - iconW) / 2, cellY + 28, 24, r->a(iconColor));
            
            // 绘制光标
            if (i == m_stickEditIndex) {
                r->drawBorderedRoundedRect(cellX + 3, cellY + 3, cellW - 3, rowH - 10, 3, 2, r->a(highlightColor));
            }
        }
    }
}

bool MacroEditGui::handleStickEditInput(u64 keysDown) {
    if (keysDown & HidNpadButton_AnyLeft) {
        m_stickEditIndex = (m_stickEditIndex % 8 == 0) ? m_stickEditIndex + 7 : m_stickEditIndex - 1;
        return true;
    }
    if (keysDown & HidNpadButton_AnyRight) {
        m_stickEditIndex = (m_stickEditIndex % 8 == 7) ? m_stickEditIndex - 7 : m_stickEditIndex + 1;
        return true;
    }
    if (keysDown & HidNpadButton_AnyUp) {
        m_stickEditIndex = (m_stickEditIndex + 8) % 16;
        return true;
    }
    if (keysDown & HidNpadButton_AnyDown) {
        m_stickEditIndex = (m_stickEditIndex + 8) % 16;
        return true;
    }
    if (keysDown & HidNpadButton_A) {
        StickDir dir = static_cast<StickDir>((m_stickEditIndex % 8) + 1);  // +1跳过None
        if (m_stickEditIndex < 8) {
            m_editingStickL = dir;
        } else {
            m_editingStickR = dir;
        }
        return true;
    }
    if (keysDown & HidNpadButton_Plus) {
        MacroData::setActionStick(m_menuSelStart, m_editingStickL, m_editingStickR);
        m_stickEditMode = false;
        m_selectMode = false;
        m_menuMode = false;
        m_menuLevel = 0;
        m_parentIndex = -1;
        return true;
    }
    if (keysDown & HidNpadButton_B) {
        m_stickEditMode = false;
        return true;
    }
    return true;
}

void MacroEditGui::enterStickEditMode() {
    auto& actions = MacroData::getActions();
    m_editingStickL = actions[m_menuSelStart].stickL;
    m_editingStickR = actions[m_menuSelStart].stickR;
    m_stickEditIndex = 0;
    m_stickEditMode = true;
}

bool MacroEditGui::handleDurationEditInput(u64 keysDown) {
    auto& actions = MacroData::getActions();
    if (keysDown & HidNpadButton_AnyLeft) {
        m_editDigitIndex = (m_editDigitIndex + 4) % 5;
        return true;
    }
    if (keysDown & HidNpadButton_AnyRight) {
        m_editDigitIndex = (m_editDigitIndex + 1) % 5;
        return true;
    }
    if (keysDown & HidNpadButton_AnyUp) {
        s32 divisor = 1;
        for (int i = 0; i < 4 - m_editDigitIndex; i++) divisor *= 10;
        s32 digit = (m_editingDuration / divisor) % 10;
        s32 maxDigit = (m_editDigitIndex == 0) ? 8 : 9;
        digit = (digit + 1) % (maxDigit + 1);
        m_editingDuration = m_editingDuration - ((m_editingDuration / divisor) % 10) * divisor + digit * divisor;
        return true;
    }
    if (keysDown & HidNpadButton_AnyDown) {
        s32 divisor = 1;
        for (int i = 0; i < 4 - m_editDigitIndex; i++) divisor *= 10;
        s32 digit = (m_editingDuration / divisor) % 10;
        s32 maxDigit = (m_editDigitIndex == 0) ? 8 : 9;
        digit = (digit + maxDigit) % (maxDigit + 1);
        m_editingDuration = m_editingDuration - ((m_editingDuration / divisor) % 10) * divisor + digit * divisor;
        return true;
    }
    if (keysDown & HidNpadButton_Plus) {
        s32 currentActionMs = actions[m_menuSelStart].duration;
        s32 totalMs = MacroData::getBasicInfo().durationMs;
        s32 maxEditMs = 80000 - (totalMs - currentActionMs);
        if (m_editingDuration > maxEditMs) return true;
        MacroData::setActionDuration(m_menuSelStart, m_menuSelEnd, m_editingDuration);
        m_durationEditMode = false;
        m_selectMode = false;
        m_menuMode = false;
        m_menuLevel = 0;
        m_parentIndex = -1;
        return true;
    }
    if (keysDown & HidNpadButton_B) {
        m_durationEditMode = false;
        return true;
    }
    return true;
}

void MacroEditGui::executeDeleteAction() {
    MacroData::deleteActions(m_menuSelStart, m_menuSelEnd);
    m_selectedIndex = (m_menuSelStart > 0) ? m_menuSelStart - 1 : 0;
    s32 itemTop = m_selectedIndex * ITEM_HEIGHT;
    s32 itemBottom = itemTop + ITEM_HEIGHT;
    if (itemTop < m_scrollOffset) m_scrollOffset = itemTop;
    else if (itemBottom > m_scrollOffset + m_listHeight) m_scrollOffset = itemBottom - m_listHeight;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
}

void MacroEditGui::enterDurationEditMode() {
    auto& actions = MacroData::getActions();
    m_editingDuration = actions[m_menuSelStart].duration;
    m_editDigitIndex = 4;
    m_durationEditMode = true;
}

void MacroEditGui::enterButtonEditMode() {
    auto& actions = MacroData::getActions();
    m_editingButtons = actions[m_menuSelStart].buttons;
    m_buttonEditIndex = 0;
    m_buttonEditMode = true;
}

bool MacroEditGui::handleMenuInput(u64 keysDown) {
    // 获取当前层级菜单信息
    const MenuDef* items = (m_menuLevel == 0) ? ROOT_MENU : ROOT_MENU[m_parentIndex].children;
    int count = (m_menuLevel == 0) ? ROOT_MENU_COUNT : ROOT_MENU[m_parentIndex].childCount;
    
    if (keysDown & HidNpadButton_AnyLeft) {
        int next = (m_menuIndex + count - 1) % count;
        for (int i = 0; i < count; i++) {
            if (isMenuItemEnabled(next)) break;
            next = (next + count - 1) % count;
        }
        m_menuIndex = next;
        return true;
    }
    if (keysDown & HidNpadButton_AnyRight) {
        int next = (m_menuIndex + 1) % count;
        for (int i = 0; i < count; i++) {
            if (isMenuItemEnabled(next)) break;
            next = (next + 1) % count;
        }
        m_menuIndex = next;
        return true;
    }
    if (keysDown & HidNpadButton_A) {
        if (isMenuItemEnabled(m_menuIndex)) {
            // 如果有子菜单，进入子菜单
            if (items[m_menuIndex].children != nullptr) {
                m_parentIndex = m_menuIndex;
                m_menuLevel = 1;
                m_menuIndex = 0;
                // 跳过禁用项
                int subCount = items[m_parentIndex].childCount;
                while (m_menuIndex < subCount && !isMenuItemEnabled(m_menuIndex)) {
                    m_menuIndex++;
                }
                return true;
            }
            // 根据菜单项执行功能
            auto& actions = MacroData::getActions();
            MenuId id = getMenuId(m_menuLevel, m_parentIndex, m_menuIndex);
            switch (id) {
                case MenuId::Duration: enterDurationEditMode(); return true;
                case MenuId::Buttons: enterButtonEditMode(); return true;
                case MenuId::StickModify: enterStickEditMode(); return true;
                case MenuId::DeleteAction: executeDeleteAction(); break;
                case MenuId::InsertBefore: MacroData::insertAction(m_menuSelStart, true); break;
                case MenuId::InsertAfter: MacroData::insertAction(m_menuSelEnd, false); break;
                case MenuId::CopyBefore: MacroData::copyActions(m_menuSelStart, m_menuSelEnd, true); break;
                case MenuId::CopyAfter: MacroData::copyActions(m_menuSelStart, m_menuSelEnd, false); break;
                case MenuId::ResetAction: MacroData::resetActions(m_menuSelStart, m_menuSelEnd); break;
                case MenuId::StickDeleteAll: MacroData::clearStick(m_menuSelStart, m_menuSelEnd, MacroData::StickTarget::Both); break;
                case MenuId::StickDeleteL: MacroData::clearStick(m_menuSelStart, m_menuSelEnd, MacroData::StickTarget::Left); break;
                case MenuId::StickDeleteR: MacroData::clearStick(m_menuSelStart, m_menuSelEnd, MacroData::StickTarget::Right); break;
                case MenuId::MoveUp:
                    if (m_menuSelStart > 0) {
                        MacroData::moveAction(m_menuSelStart - 1, m_menuSelEnd + 1);
                        m_selectedIndex--;
                    }
                    break;
                case MenuId::MoveDown:
                    if (m_menuSelEnd < (s32)actions.size() - 1) {
                        MacroData::moveAction(m_menuSelEnd + 1, m_menuSelStart);
                        m_selectedIndex++;
                    }
                    break;
                default: break;
            }
            m_selectMode = false;
            m_menuMode = false;
            m_menuLevel = 0;
            m_parentIndex = -1;
        }
        return true;
    }
    if (keysDown & HidNpadButton_B) {
        // 如果在子菜单，返回上一级
        if (m_menuLevel > 0) {
            m_menuIndex = m_parentIndex;
            m_menuLevel = 0;
            m_parentIndex = -1;
            return true;
        }
        // 否则退出菜单模式
        m_selectMode = false;
        m_menuMode = false;
        return true;
    }
    return true;
}

bool MacroEditGui::handleNormalInput(u64 keysDown, u64 keysHeld) {
    auto& actions = MacroData::getActions();
    s32 maxIndex = actions.size() - 1;
    
    if (keysDown & HidNpadButton_A) {
        if (!m_selectMode) {
            m_selectMode = true;
            m_selectAnchor = m_selectedIndex;
        }
        return true;
    }
    if (keysDown & HidNpadButton_B) {
        if (m_selectMode) {
            m_selectMode = false;
            m_selectAnchor = -1;
            return true;
        }
    }
    if (keysDown & HidNpadButton_X) {
        if (MacroData::undo()) {
            if (m_selectedIndex >= (s32)actions.size()) m_selectedIndex = actions.size() - 1;
            m_selectMode = false;
            m_selectAnchor = -1;
            return true;
        }
    }
    if (keysDown & HidNpadButton_StickL) {
        if (m_bakMacro && MacroData::loadBakMacroData()) {
            MacroData::undoCleanup();
            m_bakMacro = false;
            m_selectedIndex = 0;
            m_scrollOffset = 0;
            m_selectMode = false;
            m_selectAnchor = -1;
            return true;
        }
    }
    if (keysDown & HidNpadButton_Plus) {
        if (!m_selectMode) {
            
            MacroData::saveForEdit();
            Refresh::RefrRequest(Refresh::MacroGameList); 
            g_ipcManager.sendReloadMacroCommand();
            tsl::swapTo<MacroViewGui>(SwapDepth(2), MacroData::getFilePath(), m_gameName, m_isRecord);
            return true;
        }
    }
    if (keysDown & HidNpadButton_Minus) {
        if (m_selectMode) {
            m_menuSelStart = std::min(m_selectAnchor, m_selectedIndex);
            m_menuSelEnd = std::max(m_selectAnchor, m_selectedIndex);
            m_menuMode = true;
            m_menuIndex = 0;
            while (!isMenuItemEnabled(m_menuIndex) && m_menuIndex < ROOT_MENU_COUNT - 1) m_menuIndex++;
            return true;
        }
    }
    
    bool moveUp = false, moveDown = false, pageUp = false, pageDown = false;
    if (keysDown & HidNpadButton_AnyUp) { moveUp = true; m_repeatCounter = 0; }
    else if (keysHeld & HidNpadButton_AnyUp) {
        m_repeatCounter++;
        if (m_repeatCounter > REPEAT_DELAY && (m_repeatCounter - REPEAT_DELAY) % REPEAT_RATE == 0) moveUp = true;
    }
    if (keysDown & HidNpadButton_AnyDown) { moveDown = true; m_repeatCounter = 0; }
    else if (keysHeld & HidNpadButton_AnyDown) {
        m_repeatCounter++;
        if (m_repeatCounter > REPEAT_DELAY && (m_repeatCounter - REPEAT_DELAY) % REPEAT_RATE == 0) moveDown = true;
    }
    if (keysDown & HidNpadButton_L) { pageUp = true; m_repeatCounter = 0; }
    else if (keysHeld & HidNpadButton_L) {
        m_repeatCounter++;
        if (m_repeatCounter > REPEAT_DELAY && (m_repeatCounter - REPEAT_DELAY) % REPEAT_RATE == 0) pageUp = true;
    }
    if (keysDown & HidNpadButton_R) { pageDown = true; m_repeatCounter = 0; }
    else if (keysHeld & HidNpadButton_R) {
        m_repeatCounter++;
        if (m_repeatCounter > REPEAT_DELAY && (m_repeatCounter - REPEAT_DELAY) % REPEAT_RATE == 0) pageDown = true;
    }
    if (!(keysHeld & (HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_L | HidNpadButton_R))) m_repeatCounter = 0;
    
    if (moveUp && maxIndex >= 0) {
        if (m_selectedIndex > 0) {
            m_selectedIndex--;
            s32 itemTop = m_selectedIndex * ITEM_HEIGHT;
            if (itemTop < m_scrollOffset + ITEM_HEIGHT) {
                m_scrollOffset -= ITEM_HEIGHT;
                if (m_scrollOffset < 0) m_scrollOffset = 0;
            }
        } else {
            m_selectedIndex = maxIndex;
            s32 itemBottom = (m_selectedIndex + 1) * ITEM_HEIGHT;
            while (itemBottom > m_scrollOffset + m_listHeight) {
                m_scrollOffset += ITEM_HEIGHT;
            }
        }
        return true;
    }
    if (moveDown && maxIndex >= 0) {
        if (m_selectedIndex < maxIndex) {
            m_selectedIndex++;
            s32 itemBottom = (m_selectedIndex + 1) * ITEM_HEIGHT;
            if (itemBottom > m_scrollOffset + m_listHeight) m_scrollOffset += ITEM_HEIGHT;
        } else {
            m_selectedIndex = 0;
            m_scrollOffset = 0;
        }
        return true;
    }
    
    // 翻页功能（非选择模式下）
    if (!m_selectMode && maxIndex >= 0) {
        s32 pageSize = m_listHeight / ITEM_HEIGHT;
        if (pageSize < 1) pageSize = 1;
        
        if (pageUp) {
            m_selectedIndex -= pageSize;
            if (m_selectedIndex < 0) m_selectedIndex = 0;
            s32 itemTop = m_selectedIndex * ITEM_HEIGHT;
            while (itemTop < m_scrollOffset) {
                m_scrollOffset -= ITEM_HEIGHT;
            }
            if (m_scrollOffset < 0) m_scrollOffset = 0;
            return true;
        }
        if (pageDown) {
            m_selectedIndex += pageSize;
            if (m_selectedIndex > maxIndex) m_selectedIndex = maxIndex;
            s32 itemBottom = (m_selectedIndex + 1) * ITEM_HEIGHT;
            while (itemBottom > m_scrollOffset + m_listHeight) {
                m_scrollOffset += ITEM_HEIGHT;
            }
            return true;
        }
    }
    
    return false;
}

bool MacroEditGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) {
    if (m_buttonEditMode) return handleButtonEditInput(keysDown);
    if (m_durationEditMode) return handleDurationEditInput(keysDown);
    if (m_stickEditMode) return handleStickEditInput(keysDown);
    if (m_menuMode) return handleMenuInput(keysDown);
    return handleNormalInput(keysDown, keysHeld);
}
