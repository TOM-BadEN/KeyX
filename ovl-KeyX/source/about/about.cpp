#include "about.hpp"
#include "qrcodegen.hpp"
#include "language.hpp"

using qrcodegen::QrCode;
using qrcodegen::QrSegment;

static const char* WECHAT_PAY_URL = "wxp://f2f0mMaZS-xnKyAZaTyn813TQRZHTRKPzI6UWxldiWDYemRIq8Stt_LPfd1sLyV4v1jR";
static const char* PAYPAL_URL = "https://www.paypal.com/qrcodes/p2pqrc/7CQ7FTPN26AJ8";

// 关于插件界面构造函数
AboutPlugin::AboutPlugin()
{
    
}

// 析构函数
AboutPlugin::~AboutPlugin()
{
    tsl::clearGlyphCacheNow.store(true);
}

// 创建关于插件界面UI
tsl::elm::Element* AboutPlugin::createUI()
{
    // 创建覆盖层框架，设置标题和副标题
    auto frame = new tsl::elm::OverlayFrame("关于", APP_VERSION_STRING);

    // 创建主列表容器
    auto mainList = new tsl::elm::List();

    // 创建带有视觉效果的文本显示区域
    auto aboutText = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
        // 定义关于插件的文本内容
        const char* aboutInfo[] = {
            "KeyX",
            " • 按键助手",
            "",
            "作者：",
            " • TOM",
            "",
            "鹅群：",
            " • 1051287661",
            "",
            "功能介绍：",
            " • 无视游戏限制自由分配按键",
            " • 按键宏支持单次与循环播放",
            " • 连发功能已适配全部机型",
            " • 可自定义连发速度和间隔",
            " • 提供全局和游戏专用配置",
            "",
            "内存占用：",
            " • 系统模块内存约占用 1.06 MB",
            " • 通知弹窗触发时占用 688 KB",
            " • 通知弹窗仅在触发时占用内存",
            "",
            "感谢使用本插件！"
        };
        
        const size_t lineCount = sizeof(aboutInfo) / sizeof(aboutInfo[0]);
        const s32 lineHeight = 22;  // 增大行高
        const s32 fontSize = 20;    // 整体字号加大
        const s32 titleFontSize = 24;  // 0、1行字体更大
        const s32 padding = 20;
        
        // 计算文本总高度
        s32 totalTextHeight = lineCount * lineHeight;
        
        // 计算垂直居中的起始位置
        s32 startY = y + (h - totalTextHeight) / 2 + 10;
        
        // 绘制文本内容
        for (size_t i = 0; i < lineCount; i++) {
            s32 textY = startY + i * lineHeight;

            // 根据行号和内容类型选择颜色和字体大小
            tsl::Color textColor = {0xFF, 0xFF, 0xFF, 0xFF}; // 默认白色
            s32 currentFontSize = fontSize;
            s32 textX = x + padding;
            
            if (i == 0) { // 第0行：亮蓝色，大字体
                textColor = {0x66, 0xCC, 0xFF, 0xFF}; // 亮蓝色
                currentFontSize = titleFontSize;
            } else if (i == 1) { // 第1行：白色，大字体
                textColor = {0xFF, 0xFF, 0xFF, 0xFF}; // 白色
                currentFontSize = fontSize;
            } else if (strstr(aboutInfo[i], "：") != nullptr || strstr(aboutInfo[i], "！") != nullptr ) { // 带冒号的行：亮蓝色
                textColor = {0x66, 0xCC, 0xFF, 0xFF}; // 亮蓝色
            } else { // 不带冒号的行：白色
                textColor = {0xFF, 0xFF, 0xFF, 0xFF}; // 白色
            }
            
            renderer->drawString(aboutInfo[i], false, textX, textY, currentFontSize, textColor);
        }
        
        // 捐赠二维码 - 根据语言只生成需要的1个
        static bool isChinese = LanguageManager::isSimplifiedChinese();
        static QrCode qr = QrCode::encodeSegments(QrSegment::makeSegments(isChinese ? WECHAT_PAY_URL : PAYPAL_URL), QrCode::Ecc::LOW, 5, 5);
        const char* donateTitle = isChinese ? "微信捐赠" : "PayPal Donate";
        
        int scale = 3;
        int margin = 6;
        int qrSize = qr.getSize();
        int totalSize = qrSize * scale;
        int qrX = x + w - 20 - totalSize - 15;
        int qrY = startY + 34;
        
        // 标题居中于二维码上方，与 KeyX 同一水平线
        auto [donateW, donateH] = renderer->getTextDimensions(donateTitle, false, titleFontSize);
        s32 donateTitleX = qrX + (totalSize - donateW) / 2;
        tsl::Color donateColor = {0x66, 0xFF, 0x66, 0xFF};  // 绿色
        renderer->drawString(donateTitle, false, donateTitleX, startY, titleFontSize, donateColor);
        
        // 绘制二维码
        renderer->drawRect(qrX - margin, qrY - margin, totalSize + margin * 2, totalSize + margin * 2, tsl::Color(0xF, 0xF, 0xF, 0xF));
        for (int qy = 0; qy < qrSize; qy++) {
            for (int qx = 0; qx < qrSize; qx++) {
                if (qr.getModule(qx, qy)) {
                    renderer->drawRect(qrX + qx * scale, qrY + qy * scale, scale, scale, tsl::Color(0x0, 0x0, 0x0, 0xF));
                }
            }
        }
    });
    
    // 设置文本区域高度（占满整个内容区域）
    mainList->addItem(aboutText, 520);

    // 将主列表设置为框架的内容
    frame->setContent(mainList);

    // 返回创建的界面元素
    return frame;
}
