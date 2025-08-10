#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cctype>

using namespace std;

enum class ActivationType { Hold, Toggle };
enum class MacroMode { FirstPerson, ThirdPerson };
enum class KeybindType { Keyboard, Mouse };
enum class MouseButton { Left, Right, Middle, X1, X2 };

struct Settings {
    ActivationType activationType;
    MacroMode macroMode;
    KeybindType keybindType;
    int keyboardVk;
    MouseButton mouseButton;
    bool autoEmote;
    int emoteNumber;
};

static atomic<bool> macroEnabled{false};
static atomic<bool> stopThreads{false};

static UINT wheelDeltaUnit = 120;
static const int kFirstPersonStepDelayMs = 4;
static const int kThirdPersonKeyTapDelayMs = 10;

string toLowerCopy(const string &s) {
    string r = s;
    transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return static_cast<char>(tolower(c)); });
    return r;
}

void sleepMs(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

void sleepMicro(int us) {
    this_thread::sleep_for(chrono::microseconds(us));
}

void printBox(const vector<string>& lines) {
    size_t width = 0;
    for (auto &l : lines) width = max(width, l.size());
    string border; border.reserve(width + 4);
    border.push_back('+');
    border.append(width + 2, '-');
    border.push_back('+');
    cout << border << "\n";
    for (auto &l : lines) {
        cout << "| " << l;
        if (width > l.size()) cout << string(width - l.size(), ' ');
        cout << " |\n";
    }
    cout << border << "\n\n";
}

bool fileExists(const string &path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

string readAllText(const string &path) {
    ifstream f(path, ios::binary);
    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeAllText(const string &path, const string &text) {
    ofstream f(path, ios::binary | ios::trunc);
    f << text;
}

bool parseJsonStringField(const string &json, const string &key, string &out) {
    string k = string("\"") + key + string("\"");
    size_t p = json.find(k);
    if (p == string::npos) return false;
    p = json.find(':', p);
    if (p == string::npos) return false;
    p = json.find('"', p);
    if (p == string::npos) return false;
    size_t q = json.find('"', p + 1);
    if (q == string::npos) return false;
    out = json.substr(p + 1, q - p - 1);
    return true;
}

bool parseJsonIntField(const string &json, const string &key, int &out) {
    string k = string("\"") + key + string("\"");
    size_t p = json.find(k);
    if (p == string::npos) return false;
    p = json.find(':', p);
    if (p == string::npos) return false;
    while (p < json.size() && (json[p] == ':' || json[p] == ' ')) p++;
    string num;
    while (p < json.size() && (isdigit(static_cast<unsigned char>(json[p])) || json[p] == '-')) { num.push_back(json[p]); p++; }
    if (num.empty()) return false;
    out = stoi(num);
    return true;
}

string toJson(const Settings &s) {
    string activation = s.activationType == ActivationType::Hold ? "hold" : "toggle";
    string mode = s.macroMode == MacroMode::FirstPerson ? "first" : "third";
    string kb = s.keybindType == KeybindType::Keyboard ? "keyboard" : "mouse";
    string mb;
    if (s.mouseButton == MouseButton::Left) mb = "left";
    else if (s.mouseButton == MouseButton::Right) mb = "right";
    else if (s.mouseButton == MouseButton::Middle) mb = "middle";
    else if (s.mouseButton == MouseButton::X1) mb = "x1";
    else mb = "x2";
    stringstream ss;
    ss << "{\n";
    ss << "  \"activation\": \"" << activation << "\",\n";
    ss << "  \"mode\": \"" << mode << "\",\n";
    ss << "  \"keybind_type\": \"" << kb << "\",\n";
    ss << "  \"keyboard_vk\": " << s.keyboardVk << ",\n";
    ss << "  \"mouse_button\": \"" << mb << "\",\n";
    ss << "  \"auto_emote\": " << (s.autoEmote ? 1 : 0) << ",\n";
    ss << "  \"emote_number\": " << s.emoteNumber << "\n";
    ss << "}\n";
    return ss.str();
}

bool loadConfig(Settings &s) {
    if (!fileExists("config.json")) return false;
    string t = readAllText("config.json");
    string activation, mode, kbt, mb;
    int vk = 0;
    if (!parseJsonStringField(t, "activation", activation)) return false;
    if (!parseJsonStringField(t, "mode", mode)) return false;
    if (!parseJsonStringField(t, "keybind_type", kbt)) return false;
    parseJsonIntField(t, "keyboard_vk", vk);
    parseJsonStringField(t, "mouse_button", mb);
    int autoEmoteInt = 0;
    int emoteNum = 0;
    parseJsonIntField(t, "auto_emote", autoEmoteInt);
    parseJsonIntField(t, "emote_number", emoteNum);
    activation = toLowerCopy(activation);
    mode = toLowerCopy(mode);
    kbt = toLowerCopy(kbt);
    mb = toLowerCopy(mb);
    if (activation == "hold") s.activationType = ActivationType::Hold; else s.activationType = ActivationType::Toggle;
    if (mode == "first") s.macroMode = MacroMode::FirstPerson; else s.macroMode = MacroMode::ThirdPerson;
    if (kbt == "keyboard") s.keybindType = KeybindType::Keyboard; else s.keybindType = KeybindType::Mouse;
    s.keyboardVk = vk;
    if (mb == "left") s.mouseButton = MouseButton::Left;
    else if (mb == "right") s.mouseButton = MouseButton::Right;
    else if (mb == "middle") s.mouseButton = MouseButton::Middle;
    else if (mb == "x1") s.mouseButton = MouseButton::X1;
    else s.mouseButton = MouseButton::X2;
    s.autoEmote = autoEmoteInt != 0;
    if (emoteNum < 1 || emoteNum > 8) emoteNum = 1;
    s.emoteNumber = emoteNum;
    return true;
}

void saveConfig(const Settings &s) {
    writeAllText("config.json", toJson(s));
}

int captureNextKeyboardVk() {
    atomic<int> capturedVk{0};
    HHOOK hook = SetWindowsHookExA(WH_KEYBOARD_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (nCode == HC_ACTION) {
            KBDLLHOOKSTRUCT *p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                int vk = static_cast<int>(p->vkCode);
                PostThreadMessageA(GetCurrentThreadId(), WM_APP + 1, static_cast<WPARAM>(vk), 0);
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }, GetModuleHandleA(nullptr), 0);
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_APP + 1) {
            capturedVk = static_cast<int>(msg.wParam);
            break;
        }
    }
    UnhookWindowsHookEx(hook);
    return capturedVk.load();
}

MouseButton captureNextMouseButton() {
    atomic<MouseButton> captured;
    HHOOK hook = SetWindowsHookExA(WH_MOUSE_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (nCode == HC_ACTION) {
            MSLLHOOKSTRUCT* p = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            if (wParam == WM_LBUTTONDOWN) PostThreadMessageA(GetCurrentThreadId(), WM_APP + 2, static_cast<WPARAM>(MouseButton::Left), 0);
            else if (wParam == WM_RBUTTONDOWN) PostThreadMessageA(GetCurrentThreadId(), WM_APP + 2, static_cast<WPARAM>(MouseButton::Right), 0);
            else if (wParam == WM_MBUTTONDOWN) PostThreadMessageA(GetCurrentThreadId(), WM_APP + 2, static_cast<WPARAM>(MouseButton::Middle), 0);
            else if (wParam == WM_XBUTTONDOWN) {
                WORD xb = HIWORD(p->mouseData);
                if (xb == XBUTTON1) PostThreadMessageA(GetCurrentThreadId(), WM_APP + 2, static_cast<WPARAM>(MouseButton::X1), 0);
                else if (xb == XBUTTON2) PostThreadMessageA(GetCurrentThreadId(), WM_APP + 2, static_cast<WPARAM>(MouseButton::X2), 0);
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }, GetModuleHandleA(nullptr), 0);
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_APP + 2) {
            captured = static_cast<MouseButton>(msg.wParam);
            break;
        }
    }
    UnhookWindowsHookEx(hook);
    return captured.load();
}

struct InputBind { KeybindType type; int vk; MouseButton mb; };

InputBind captureNextBind() {
    atomic<bool> done{false};
    atomic<InputBind*> resultPtr{nullptr};
    InputBind res{};
    HHOOK kHook = SetWindowsHookExA(WH_KEYBOARD_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            KBDLLHOOKSTRUCT *p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            InputBind* out = new InputBind();
            out->type = KeybindType::Keyboard;
            out->vk = static_cast<int>(p->vkCode);
            out->mb = MouseButton::Left;
            PostThreadMessageA(GetCurrentThreadId(), WM_APP + 3, reinterpret_cast<WPARAM>(out), 0);
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }, GetModuleHandleA(nullptr), 0);
    HHOOK mHook = SetWindowsHookExA(WH_MOUSE_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (nCode == HC_ACTION) {
            MSLLHOOKSTRUCT* p = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            InputBind* out = nullptr;
            if (wParam == WM_LBUTTONDOWN) { out = new InputBind(); out->type = KeybindType::Mouse; out->mb = MouseButton::Left; out->vk = 0; }
            else if (wParam == WM_RBUTTONDOWN) { out = new InputBind(); out->type = KeybindType::Mouse; out->mb = MouseButton::Right; out->vk = 0; }
            else if (wParam == WM_MBUTTONDOWN) { out = new InputBind(); out->type = KeybindType::Mouse; out->mb = MouseButton::Middle; out->vk = 0; }
            else if (wParam == WM_XBUTTONDOWN) {
                WORD xb = HIWORD(p->mouseData);
                if (xb == XBUTTON1) { out = new InputBind(); out->type = KeybindType::Mouse; out->mb = MouseButton::X1; out->vk = 0; }
                else if (xb == XBUTTON2) { out = new InputBind(); out->type = KeybindType::Mouse; out->mb = MouseButton::X2; out->vk = 0; }
            }
            if (out) PostThreadMessageA(GetCurrentThreadId(), WM_APP + 3, reinterpret_cast<WPARAM>(out), 0);
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }, GetModuleHandleA(nullptr), 0);
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_APP + 3) {
            InputBind* ptr = reinterpret_cast<InputBind*>(msg.wParam);
            res = *ptr;
            delete ptr;
            break;
        }
    }
    UnhookWindowsHookEx(kHook);
    UnhookWindowsHookEx(mHook);
    return res;
}

void sendKeyDown(WORD vk) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof(INPUT));
}

void sendKeyUp(WORD vk) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void sendScanDown(WORD vk) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = KEYEVENTF_SCANCODE;
    SendInput(1, &in, sizeof(INPUT));
}

void sendScanUp(WORD vk) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void sendMouseWheel(int delta) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = delta;
    SendInput(1, &in, sizeof(INPUT));
}

void runFirstPersonLoop() {
    while (!stopThreads.load()) {
        if (macroEnabled.load()) {
            sendMouseWheel(static_cast<int>(wheelDeltaUnit));
            sleepMs(kFirstPersonStepDelayMs);
            sendMouseWheel(-static_cast<int>(wheelDeltaUnit));
            sleepMs(kFirstPersonStepDelayMs);
        } else {
            sleepMs(kFirstPersonStepDelayMs);
        }
    }
}

void runThirdPersonLoop() {
    const WORD VK_I = 0x49;
    const WORD VK_O = 0x4F;
    bool iDown = false;
    bool oDown = false;
    while (!stopThreads.load()) {
        if (macroEnabled.load()) {
            sendScanDown(VK_I);
            iDown = true;
            sleepMs(kThirdPersonKeyTapDelayMs);
            if (!macroEnabled.load()) {
                if (iDown) { sendScanUp(VK_I); iDown = false; }
                if (oDown) { sendScanUp(VK_O); oDown = false; }
                continue;
            }

            sendScanDown(VK_O);
            oDown = true;
            sleepMs(kThirdPersonKeyTapDelayMs);
            if (!macroEnabled.load()) {
                if (iDown) { sendScanUp(VK_I); iDown = false; }
                if (oDown) { sendScanUp(VK_O); oDown = false; }
                continue;
            }

            sendScanUp(VK_I);
            iDown = false;
            sleepMs(kThirdPersonKeyTapDelayMs);
            if (!macroEnabled.load()) {
                if (oDown) { sendScanUp(VK_O); oDown = false; }
                continue;
            }

            sendScanUp(VK_O);
            oDown = false;
            sleepMs(kThirdPersonKeyTapDelayMs);
        } else {
            if (iDown) { sendScanUp(VK_I); iDown = false; }
            if (oDown) { sendScanUp(VK_O); oDown = false; }
            sleepMs(kThirdPersonKeyTapDelayMs);
        }
    }
    if (iDown) sendScanUp(VK_I);
    if (oDown) sendScanUp(VK_O);
}

string activationToString(ActivationType a) {
    return a == ActivationType::Hold ? "hold" : "toggle";
}

string modeToString(MacroMode m) {
    return m == MacroMode::FirstPerson ? "1st person" : "3rd person";
}

string keybindTypeToString(KeybindType k) {
    return k == KeybindType::Keyboard ? "keyboard" : "mouse";
}

string mouseButtonToString(MouseButton b) {
    if (b == MouseButton::Left) return "left";
    if (b == MouseButton::Right) return "right";
    if (b == MouseButton::Middle) return "middle";
    if (b == MouseButton::X1) return "x1";
    return "x2";
}

void clearConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return;
    DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
    COORD coord = {0, 0};
    DWORD written = 0;
    FillConsoleOutputCharacterA(h, ' ', size, coord, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, size, coord, &written);
    SetConsoleCursorPosition(h, coord);
}

static HANDLE statusEvent = nullptr;

struct ConsoleSize { short cols; short rows; };

ConsoleSize getConsoleSize() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return {80, 25};
    short cols = static_cast<short>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    short rows = static_cast<short>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    return {cols, rows};
}

void hideCursor() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci{};
    if (GetConsoleCursorInfo(h, &ci)) {
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(h, &ci);
    }
}

wstring utf8ToWide(const string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    wstring ws;
    ws.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

string formatBindString(const Settings &s) {
    if (s.keybindType == KeybindType::Keyboard) {
        stringstream ss; ss << "VK 0x" << hex << uppercase << s.keyboardVk;
        return ss.str();
    } else {
        return string("mouse ") + mouseButtonToString(s.mouseButton);
    }
}

static WORD g_defaultAttributes = 0;

void setColor(WORD attr) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
}

void resetColor() {
    if (g_defaultAttributes) SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_defaultAttributes);
}

WORD colorRgb(bool r, bool g, bool b, bool bright) {
    WORD a = 0;
    if (r) a |= FOREGROUND_RED;
    if (g) a |= FOREGROUND_GREEN;
    if (b) a |= FOREGROUND_BLUE;
    if (bright) a |= FOREGROUND_INTENSITY;
    return a;
}

void writeCenteredLine(const wstring& text, int cols, WORD attr, bool useAttr) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    int leftPad = 0;
    if (cols > 0) leftPad = max(0, cols - (int)text.size()) / 2;
    if (leftPad > 0) {
        wstring spaces(leftPad, L' ');
        WriteConsoleW(h, spaces.c_str(), (DWORD)spaces.size(), &written, nullptr);
    }
    if (useAttr) setColor(attr); else resetColor();
    WriteConsoleW(h, text.c_str(), (DWORD)text.size(), &written, nullptr);
    resetColor();
    WriteConsoleW(h, L"\n", 1, &written, nullptr);
}

void drawCenteredUI(const Settings &s, bool running) {
    vector<wstring> content;
    content.push_back(L"insidingforfeds macro");
    content.push_back(running ? L"[ RUNNING ]" : L"[ STOPPED ]");
    content.push_back(utf8ToWide(string("bind: ") + formatBindString(s)));
    {
        wstring mode = utf8ToWide(modeToString(s.macroMode));
        wstring act = utf8ToWide(activationToString(s.activationType));
        wstring line = mode + L"  |  " + act;
        if (s.autoEmote) line += L"  |  auto emote on";
        content.push_back(line);
    }
    content.push_back(L"");
    content.push_back(L"press your bind to start/stop");

    size_t width = 0;
    for (auto &l : content) width = max(width, l.size());
    wstring top = L"+" + wstring(width + 2, L'-') + L"+";

    ConsoleSize cs = getConsoleSize();
    int totalLines = (int)content.size() + 2;
    int topPad = 0;
    if (cs.rows > 0) topPad = max(0, (int)cs.rows - totalLines) / 2;

    clearConsole();
    for (int i = 0; i < topPad; ++i) writeCenteredLine(L"", cs.cols, 0, false);

    writeCenteredLine(top, cs.cols, 0, false);
    for (size_t i = 0; i < content.size(); ++i) {
        wstring line = L"| " + content[i] + wstring(width - content[i].size(), L' ') + L" |";
        bool isStatus = (i == 1);
        WORD attr = isStatus ? (running ? colorRgb(false, true, false, true) : colorRgb(true, false, false, true)) : 0;
        writeCenteredLine(line, cs.cols, attr, isStatus);
    }
    writeCenteredLine(top, cs.cols, 0, false);
}

void drawCenteredPanel(const vector<string>& lines) {
    vector<wstring> content;
    content.reserve(lines.size());
    for (auto &l : lines) content.push_back(utf8ToWide(l));

    size_t width = 0;
    for (auto &l : content) width = max(width, l.size());
    wstring top = L"+" + wstring(width + 2, L'-') + L"+";

    ConsoleSize cs = getConsoleSize();
    int totalLines = (int)content.size() + 2;
    int topPad = 0;
    if (cs.rows > 0) topPad = max(0, (int)cs.rows - totalLines) / 2;

    clearConsole();
    for (int i = 0; i < topPad; ++i) writeCenteredLine(L"", cs.cols, 0, false);

    writeCenteredLine(top, cs.cols, 0, false);
    for (size_t i = 0; i < content.size(); ++i) {
        wstring line = L"| " + content[i] + wstring(width - content[i].size(), L' ') + L" |";
        bool isTitle = (i == 0);
        WORD attr = isTitle ? colorRgb(false, true, true, true) : 0;
        writeCenteredLine(line, cs.cols, attr, isTitle);
    }
    writeCenteredLine(top, cs.cols, 0, false);
}

void printCenteredPrompt(const string& prompt) {
    ConsoleSize cs = getConsoleSize();
    wstring wp = utf8ToWide(prompt);
    int leftPad = 0;
    if (cs.cols > 0) leftPad = max(0, (int)cs.cols - (int)wp.size()) / 2;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (leftPad > 0) {
        wstring spaces(leftPad, L' ');
        WriteConsoleW(h, spaces.c_str(), (DWORD)spaces.size(), &written, nullptr);
    }
    WriteConsoleW(h, wp.c_str(), (DWORD)wp.size(), &written, nullptr);
}

static atomic<bool> g_holdRequest{false};

void pressTap(WORD vk) {
    sendScanDown(vk);
    sleepMs(1);
    sendScanUp(vk);
}

void runAutoEmoteThenEnable(int emoteNumber) {
    const WORD VK_DOT = 0xBE;
    int n = emoteNumber;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    pressTap(VK_DOT);
    sleepMs(50);
    WORD vkNum = static_cast<WORD>(0x30 + n);
    pressTap(vkNum);
    sleepMs(40);
}

void setMacroEnabled(bool enabled) {
    bool previous = macroEnabled.exchange(enabled);
    if (statusEvent && previous != enabled) {
        SetEvent(statusEvent);
    }
}

struct MonitorState { ActivationType act; KeybindType type; int vk; MouseButton mb; bool autoEmote; int emoteNum; };
static MonitorState g_monitorState;

void startInputMonitor(const MonitorState &ms) {
    g_monitorState = ms;
    thread([]{
        HHOOK kHook = nullptr, mHook = nullptr;
        if (g_monitorState.type == KeybindType::Keyboard) {
            kHook = SetWindowsHookExA(WH_KEYBOARD_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
                if (nCode == HC_ACTION) {
                    KBDLLHOOKSTRUCT *p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
                    static bool pressed = false;
                    if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && p->vkCode == (DWORD)g_monitorState.vk) {
                        if (g_monitorState.act == ActivationType::Toggle) {
                            if (!pressed) {
                                bool cur = macroEnabled.load();
                                if (!cur) {
                                    if (g_monitorState.autoEmote) {
                                        thread([]{
                                            runAutoEmoteThenEnable(g_monitorState.emoteNum);
                                            setMacroEnabled(true);
                                        }).detach();
                                    } else {
                                        setMacroEnabled(true);
                                    }
                                } else {
                                    setMacroEnabled(false);
                                }
                            }
                            pressed = true;
                        } else {
                            if (!pressed) {
                                g_holdRequest.store(true);
                                if (g_monitorState.autoEmote) {
                                    thread([]{
                                        runAutoEmoteThenEnable(g_monitorState.emoteNum);
                                        if (g_holdRequest.load()) setMacroEnabled(true);
                                    }).detach();
                                } else {
                                    setMacroEnabled(true);
                                }
                                pressed = true;
                            }
                        }
                    } else if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && p->vkCode == (DWORD)g_monitorState.vk) {
                        if (g_monitorState.act == ActivationType::Hold) setMacroEnabled(false);
                        g_holdRequest.store(false);
                        pressed = false;
                    }
                }
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }, GetModuleHandleA(nullptr), 0);
        } else {
            mHook = SetWindowsHookExA(WH_MOUSE_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
                if (nCode == HC_ACTION) {
                    MSLLHOOKSTRUCT* p = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
                    static bool pressed = false;
                    bool isDown = false, isUp = false, match = false;
                    if (g_monitorState.mb == MouseButton::Left) { isDown = wParam == WM_LBUTTONDOWN; isUp = wParam == WM_LBUTTONUP; match = isDown || isUp; }
                    else if (g_monitorState.mb == MouseButton::Right) { isDown = wParam == WM_RBUTTONDOWN; isUp = wParam == WM_RBUTTONUP; match = isDown || isUp; }
                    else if (g_monitorState.mb == MouseButton::Middle) { isDown = wParam == WM_MBUTTONDOWN; isUp = wParam == WM_MBUTTONUP; match = isDown || isUp; }
                    else if (g_monitorState.mb == MouseButton::X1 || g_monitorState.mb == MouseButton::X2) {
                        if (wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP) {
                            WORD xb = HIWORD(p->mouseData);
                            if ((g_monitorState.mb == MouseButton::X1 && xb == XBUTTON1) || (g_monitorState.mb == MouseButton::X2 && xb == XBUTTON2)) {
                                isDown = wParam == WM_XBUTTONDOWN;
                                isUp = wParam == WM_XBUTTONUP;
                                match = true;
                            }
                        }
                    }
                    if (match) {
                        if (isDown) {
                            if (g_monitorState.act == ActivationType::Toggle) {
                                if (!pressed) {
                                    bool cur = macroEnabled.load();
                                    if (!cur) {
                                        if (g_monitorState.autoEmote) {
                                            thread([]{
                                                runAutoEmoteThenEnable(g_monitorState.emoteNum);
                                                setMacroEnabled(true);
                                            }).detach();
                                        } else {
                                            setMacroEnabled(true);
                                        }
                                    } else {
                                        setMacroEnabled(false);
                                    }
                                }
                                pressed = true;
                            } else {
                                if (!pressed) {
                                    g_holdRequest.store(true);
                                    if (g_monitorState.autoEmote) {
                                        thread([]{
                                            runAutoEmoteThenEnable(g_monitorState.emoteNum);
                                            if (g_holdRequest.load()) setMacroEnabled(true);
                                        }).detach();
                                    } else {
                                        setMacroEnabled(true);
                                    }
                                    pressed = true;
                                }
                            }
                        } else if (isUp) {
                            if (g_monitorState.act == ActivationType::Hold) setMacroEnabled(false);
                            g_holdRequest.store(false);
                            pressed = false;
                        }
                    }
                }
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }, GetModuleHandleA(nullptr), 0);
        }
        MSG msg;
        while (GetMessageA(&msg, nullptr, 0, 0)) {
            if (stopThreads.load()) break;
        }
        if (kHook) UnhookWindowsHookEx(kHook);
        if (mHook) UnhookWindowsHookEx(mHook);
    }).detach();
}

int main() {
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    hideCursor();
    statusEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO csbiInit{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbiInit)) {
        g_defaultAttributes = csbiInit.wAttributes;
    }

    vector<string> header = {
        "made by @insidingforfeds on dc,",
        "dm for source ;p",
        "cpp version"
    };
    printBox(header);

    Settings s{};
    s.autoEmote = false;
    s.emoteNumber = 1;
    bool haveConfig = loadConfig(s);
    if (haveConfig) {
        vector<string> lines = {
            "setup",
            "found saved settings"
        };
        drawCenteredPanel(lines);
        printCenteredPrompt("Use last config? (y/n): ");
        string ans; getline(cin, ans);
        if (toLowerCopy(ans) == "y" || toLowerCopy(ans) == "yes") {
        } else {
            haveConfig = false;
        }
    }

    if (!haveConfig) {
        {
            vector<string> lines = {
                "setup",
                "choose activation",
                "1 = hold, 2 = toggle"
            };
            drawCenteredPanel(lines);
            printCenteredPrompt("Activation type: ");
            string a; getline(cin, a); a = toLowerCopy(a);
            if (a == "1" || a == "hold" || a == "hold key") s.activationType = ActivationType::Hold; else s.activationType = ActivationType::Toggle;
        }
        {
            vector<string> lines = {
                "setup",
                "choose mode",
                "1 = 1st person, 2 = 3rd person"
            };
            drawCenteredPanel(lines);
            printCenteredPrompt("Macro mode: ");
            string m; getline(cin, m); m = toLowerCopy(m);
            if (m == "1" || m == "first" || m == "1st" || m == "one") s.macroMode = MacroMode::FirstPerson; else s.macroMode = MacroMode::ThirdPerson;
        }
        {
            vector<string> lines = {
                "setup",
                "bind a key or mouse button",
                "press any key or mouse button now"
            };
            drawCenteredPanel(lines);
            Sleep(300);
            InputBind b = captureNextBind();
            s.keybindType = b.type;
            if (b.type == KeybindType::Keyboard) {
                s.keyboardVk = b.vk;
                vector<string> conf = { "setup", "input captured", (string)"VK 0x" + (static_cast<stringstream&&>(stringstream() << hex << uppercase << s.keyboardVk)).str() };
                drawCenteredPanel(conf);
            } else {
                s.mouseButton = b.mb;
                vector<string> conf = { "setup", "input captured", string("mouse ") + mouseButtonToString(s.mouseButton) };
                drawCenteredPanel(conf);
            }
        }
        {
            vector<string> lines = {
                "setup",
                "auto emote",
                "automatically emote so you don't have to do the process"
            };
            drawCenteredPanel(lines);
            printCenteredPrompt("Enable auto emote? (y/n): ");
            string ae; getline(cin, ae); ae = toLowerCopy(ae);
            if (ae == "y" || ae == "yes") {
                s.autoEmote = true;
                while (true) {
                    vector<string> lines2 = { "setup", "choose emote number", "1 - 8" };
                    drawCenteredPanel(lines2);
                    printCenteredPrompt("Emote number: ");
                    string en; getline(cin, en);
                    int n = 0;
                    try { n = stoi(en); } catch (...) { n = 0; }
                    if (n >= 1 && n <= 8) { s.emoteNumber = n; break; }
                }
            } else {
                s.autoEmote = false;
            }
        }
        {
            vector<string> lines = { "setup", "save settings for next launch?" };
            drawCenteredPanel(lines);
            printCenteredPrompt("Save config? (y/n): ");
            string sv; getline(cin, sv);
            if (toLowerCopy(sv) == "y" || toLowerCopy(sv) == "yes") saveConfig(s);
        }
    }

    thread worker;
    if (s.macroMode == MacroMode::FirstPerson) worker = thread(runFirstPersonLoop); else worker = thread(runThirdPersonLoop);

    MonitorState ms{ s.activationType, s.keybindType, s.keyboardVk, s.mouseButton, s.autoEmote, s.emoteNumber };
    startInputMonitor(ms);

    bool last = macroEnabled.load();
    clearConsole();
    drawCenteredUI(s, last);

    ConsoleSize lastSize = getConsoleSize();
    while (true) {
        DWORD waitRes = WaitForSingleObject(statusEvent, 500);
        if (waitRes == WAIT_OBJECT_0) {
            ResetEvent(statusEvent);
        }
        bool cur = macroEnabled.load();
        ConsoleSize curSize = getConsoleSize();
        bool sizeChanged = (curSize.cols != lastSize.cols || curSize.rows != lastSize.rows);
        if (cur != last || sizeChanged) {
            drawCenteredUI(s, cur);
            last = cur;
            lastSize = curSize;
        }
    }

    stopThreads.store(true);
    if (worker.joinable()) worker.join();
    timeEndPeriod(1);
    return 0;
} 