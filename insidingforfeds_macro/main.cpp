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
};

static atomic<bool> macroEnabled{false};
static atomic<bool> stopThreads{false};

static UINT wheelDeltaUnit = 50;
static const int kFirstPersonStepDelayMs = 10;
static const int kThirdPersonKeyTapDelayMs = 5;

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
    ss << "  \"mouse_button\": \"" << mb << "\"\n";
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
            // hold i
            sendScanDown(VK_I);
            iDown = true;
            sleepMs(kThirdPersonKeyTapDelayMs);
            if (!macroEnabled.load()) {
                if (iDown) { sendScanUp(VK_I); iDown = false; }
                if (oDown) { sendScanUp(VK_O); oDown = false; }
                continue;
            }

            // hold o
            sendScanDown(VK_O);
            oDown = true;
            sleepMs(kThirdPersonKeyTapDelayMs);
            if (!macroEnabled.load()) {
                if (iDown) { sendScanUp(VK_I); iDown = false; }
                if (oDown) { sendScanUp(VK_O); oDown = false; }
                continue;
            }

            // release i
            sendScanUp(VK_I);
            iDown = false;
            sleepMs(kThirdPersonKeyTapDelayMs);
            if (!macroEnabled.load()) {
                if (oDown) { sendScanUp(VK_O); oDown = false; }
                continue;
            }

            // release o
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

struct MonitorState { ActivationType act; KeybindType type; int vk; MouseButton mb; };
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
                            if (!pressed) macroEnabled.store(!macroEnabled.load());
                            pressed = true;
                        } else {
                            macroEnabled.store(true);
                            pressed = true;
                        }
                    } else if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && p->vkCode == (DWORD)g_monitorState.vk) {
                        if (g_monitorState.act == ActivationType::Hold) macroEnabled.store(false);
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
                                if (!pressed) macroEnabled.store(!macroEnabled.load());
                                pressed = true;
                            } else {
                                macroEnabled.store(true);
                                pressed = true;
                            }
                        } else if (isUp) {
                            if (g_monitorState.act == ActivationType::Hold) macroEnabled.store(false);
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

    vector<string> header = {
        "made by @insidingforfeds on dc,",
        "dm for source ;p",
        "cpp version"
    };
    printBox(header);

    Settings s{};
    bool haveConfig = loadConfig(s);
    if (haveConfig) {
        cout << "Use last config? (y/n): ";
        string ans; getline(cin, ans);
        if (toLowerCopy(ans) == "y" || toLowerCopy(ans) == "yes") {
        } else {
            haveConfig = false;
        }
    }

    if (!haveConfig) {
        cout << "Activation type (1=hold, 2=toggle): ";
        string a; getline(cin, a); a = toLowerCopy(a);
        if (a == "1" || a == "hold" || a == "hold key") s.activationType = ActivationType::Hold; else s.activationType = ActivationType::Toggle;

        cout << "Macro mode (1=1st person, 2=3rd person): ";
        string m; getline(cin, m); m = toLowerCopy(m);
        if (m == "1" || m == "first" || m == "1st" || m == "one") s.macroMode = MacroMode::FirstPerson; else s.macroMode = MacroMode::ThirdPerson;

        cout << "Press any key or mouse button to bind..." << endl;
        Sleep(300);
        InputBind b = captureNextBind();
        s.keybindType = b.type;
        if (b.type == KeybindType::Keyboard) { s.keyboardVk = b.vk; cout << "Captured VK: 0x" << hex << uppercase << s.keyboardVk << nouppercase << dec << endl; }
        else { s.mouseButton = b.mb; cout << "Captured Mouse: " << mouseButtonToString(s.mouseButton) << endl; }

        cout << "Save this config for next launch? (y/n): ";
        string sv; getline(cin, sv);
        if (toLowerCopy(sv) == "y" || toLowerCopy(sv) == "yes") saveConfig(s);
    }

    thread worker;
    if (s.macroMode == MacroMode::FirstPerson) worker = thread(runFirstPersonLoop); else worker = thread(runThirdPersonLoop);

    MonitorState ms{ s.activationType, s.keybindType, s.keyboardVk, s.mouseButton };
    startInputMonitor(ms);

    bool last = macroEnabled.load();
    clearConsole();
    cout << (last ? "macro running" : "macro stopped") << endl;
    cout << "bind: " << (s.keybindType == KeybindType::Keyboard ? "VK 0x" : "mouse ") << (s.keybindType == KeybindType::Keyboard ? (static_cast<stringstream&&>(stringstream() << hex << uppercase << s.keyboardVk)).str() : mouseButtonToString(s.mouseButton)) << endl;

    while (true) {
        bool cur = macroEnabled.load();
        if (cur != last) {
            clearConsole();
            cout << (cur ? "macro running" : "macro stopped") << endl;
            last = cur;
        }
        sleepMs(16);
    }

    stopThreads.store(true);
    if (worker.joinable()) worker.join();
    timeEndPeriod(1);
    return 0;
} 