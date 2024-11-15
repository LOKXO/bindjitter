    #include <windows.h>
    #include <thread>
    #include <chrono>
    #include <TlHelp32.h>
    #include <vector>
    #include <string>
    #include <map>
    #include <atomic>
    #include <cstdlib>
    
    #ifdef _MSC_VER
    #pragma comment(linker, "/manifestdependency:\"type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
    #pragma comment(linker, "/manifest:embed")
    #endif
    
    struct KeyBind {
        int key;
        int triggerKey;
        bool isToggle;
        int pressDelay;
        int unpressDelay;
        std::atomic<bool> isEnabled;
        bool wasKeyPressed;
        std::chrono::steady_clock::time_point lastToggleTime;
    
        KeyBind() : key(0), triggerKey(0), isToggle(false), pressDelay(50), 
                   unpressDelay(50), isEnabled(false), wasKeyPressed(false),
                   lastToggleTime(std::chrono::steady_clock::now()) {}
    
        KeyBind(const KeyBind& other)
            : key(other.key),
              triggerKey(other.triggerKey),
              isToggle(other.isToggle),
              pressDelay(other.pressDelay),
              unpressDelay(other.unpressDelay),
              isEnabled(other.isEnabled.load()),
              wasKeyPressed(other.wasKeyPressed),
              lastToggleTime(other.lastToggleTime) {}
    
        KeyBind& operator=(const KeyBind& other) {
            if (this != &other) {
                key = other.key;
                triggerKey = other.triggerKey;
                isToggle = other.isToggle;
                pressDelay = other.pressDelay;
                unpressDelay = other.unpressDelay;
                isEnabled.store(other.isEnabled.load());
                wasKeyPressed = other.wasKeyPressed;
                lastToggleTime = other.lastToggleTime;
            }
            return *this;
        }
    };
    
    std::vector<KeyBind> keyBinds;
    HWND mainWindow;
    HWND listBox;
    HWND addButton, editButton, removeButton;
    
    std::atomic<bool> bindsDisabled(false);
    std::chrono::steady_clock::time_point disableStartTime;
    const std::chrono::minutes disableDuration(1);
    
    DWORD GetCS2ProcessId() {
        DWORD processId = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W processEntry;
            processEntry.dwSize = sizeof(processEntry);
            if (Process32FirstW(snapshot, &processEntry)) {
                do {
                    if (_wcsicmp(processEntry.szExeFile, L"cs2.exe") == 0) {
                        processId = processEntry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snapshot, &processEntry));
            }
            CloseHandle(snapshot);
        }
        return processId;
    }
    
    std::string VKToString(int vk) {
        switch (vk) {
            case VK_SHIFT: return "SHIFT";
            case VK_CONTROL: return "CTRL";
            case VK_MENU: return "ALT";
            case VK_ESCAPE: return "ESC";
            case VK_SPACE: return "SPACE";
            case VK_TAB: return "TAB";
            case VK_RETURN: return "ENTER";
            case VK_BACK: return "BACKSPACE";
            case VK_DELETE: return "DELETE";
            case VK_LEFT: return "LEFT";
            case VK_RIGHT: return "RIGHT";
            case VK_UP: return "UP";
            case VK_DOWN: return "DOWN";
            default: break;
        }
        
        if (vk >= VK_F1 && vk <= VK_F12) {
            return "F" + std::to_string(vk - VK_F1 + 1);
        }
        
        if (vk >= 'A' && vk <= 'Z') {
            return std::string(1, static_cast<char>(vk));
        }
        
        if (vk >= '0' && vk <= '9') {
            return std::string(1, static_cast<char>(vk));
        }
        
        return std::to_string(vk);
    }
    
    int StringToVK(const std::string& str) {
        std::string upper = str;
        for (char& c : upper) {
            c = static_cast<char>(toupper(c));
        }
        
        if (upper == "SHIFT") return VK_SHIFT;
        if (upper == "CTRL") return VK_CONTROL;
        if (upper == "ALT") return VK_MENU;
        if (upper == "ESC") return VK_ESCAPE;
        if (upper == "SPACE") return VK_SPACE;
        if (upper == "TAB") return VK_TAB;
        if (upper == "ENTER") return VK_RETURN;
        if (upper == "BACKSPACE") return VK_BACK;
        if (upper == "DELETE") return VK_DELETE;
        if (upper == "LEFT") return VK_LEFT;
        if (upper == "RIGHT") return VK_RIGHT;
        if (upper == "UP") return VK_UP;
        if (upper == "DOWN") return VK_DOWN;
        
        if (upper.length() > 1 && upper[0] == 'F') {
            char* end;
            long num = std::strtol(upper.substr(1).c_str(), &end, 10);
            if (*end == '\0' && num >= 1 && num <= 24) {
                return VK_F1 + static_cast<int>(num) - 1;
            }
        }
        
        if (upper.length() == 1) {
            return static_cast<int>(upper[0]);
        }
        
        char* end;
        long result = std::strtol(str.c_str(), &end, 10);
        if (*end == '\0') {
            return static_cast<int>(result);
        }
        
        return 0;
    }
    
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    void CreateBindDialog(HWND parent, KeyBind* existingBind = nullptr) {
        static bool classRegistered = false;
        
        if (!classRegistered) {
            WNDCLASSEX wc = {0};
            wc.cbSize = sizeof(WNDCLASSEX);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = DialogProc;
            wc.hInstance = GetModuleHandle(nullptr);
            wc.lpszClassName = "BindDialogClass";
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            if (!RegisterClassEx(&wc)) {
                MessageBoxA(parent, "Failed to register dialog class.", "Error", MB_ICONERROR);
                return;
            }
            classRegistered = true;
        }
    
        HWND dialog = CreateWindowEx(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            "BindDialogClass",
            existingBind ? "Edit Bind" : "Add New Bind",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 375, 312,
            parent,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
    
        if (!dialog) {
            MessageBoxA(parent, "Failed to create dialog window.", "Error", MB_ICONERROR);
            return;
        }
    
        CreateWindow("STATIC", "Key to Bind:", WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 12, 125, 25, dialog, nullptr, GetModuleHandle(nullptr), nullptr);
        HWND keyEdit = CreateWindow("EDIT", existingBind ? VKToString(existingBind->key).c_str() : "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            150, 12, 200, 25, dialog, reinterpret_cast<HMENU>(101), GetModuleHandle(nullptr), nullptr);
    
        CreateWindow("STATIC", "Trigger Key:", WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 50, 125, 25, dialog, nullptr, GetModuleHandle(nullptr), nullptr);
        HWND triggerEdit = CreateWindow("EDIT", existingBind ? VKToString(existingBind->triggerKey).c_str() : "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            150, 50, 200, 25, dialog, reinterpret_cast<HMENU>(102), GetModuleHandle(nullptr), nullptr);
    
        HWND toggleCheck = CreateWindow("BUTTON", "Toggle Mode",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            12, 87, 125, 25, dialog, reinterpret_cast<HMENU>(103), GetModuleHandle(nullptr), nullptr);
        if (existingBind && existingBind->isToggle) {
            SendMessage(toggleCheck, BM_SETCHECK, BST_CHECKED, 0);
        }
    
        CreateWindow("STATIC", "Press Delay (ms):", WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 125, 125, 25, dialog, nullptr, GetModuleHandle(nullptr), nullptr);
        HWND pressDelayEdit = CreateWindow("EDIT", existingBind ? std::to_string(existingBind->pressDelay).c_str() : "50",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            150, 125, 200, 25, dialog, reinterpret_cast<HMENU>(104), GetModuleHandle(nullptr), nullptr);
    
        CreateWindow("STATIC", "Unpress Delay (ms):", WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 162, 125, 25, dialog, nullptr, GetModuleHandle(nullptr), nullptr);
        HWND unpressDelayEdit = CreateWindow("EDIT", existingBind ? std::to_string(existingBind->unpressDelay).c_str() : "50",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            150, 162, 200, 25, dialog, reinterpret_cast<HMENU>(105), GetModuleHandle(nullptr), nullptr);
    
        HWND okButton = CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            75, 212, 100, 37, dialog, reinterpret_cast<HMENU>(106), GetModuleHandle(nullptr), nullptr);
    
        HWND cancelButton = CreateWindow("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            187, 212, 100, 37, dialog, reinterpret_cast<HMENU>(107), GetModuleHandle(nullptr), nullptr);
    
        SetWindowLongPtr(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(existingBind));
    
        ShowWindow(dialog, SW_SHOW);
    }
    
    void UpdateBindsList() {
        SendMessage(listBox, LB_RESETCONTENT, 0, 0);
        for (const auto& bind : keyBinds) {
            std::string bindInfo = "Key: " + VKToString(bind.key) +
                (bind.isToggle ? " (Toggle)" : " (Hold)") +
                " Delays: " + std::to_string(bind.pressDelay) + "/" +
                std::to_string(bind.unpressDelay) + "ms";
            SendMessageA(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(bindInfo.c_str()));
        }
    }
    
    void CreateMainWindow() {
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "AutoKeyPresserClass";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassEx(&wc)) {
            MessageBoxA(nullptr, "Failed to register main window class.", "Error", MB_ICONERROR);
            return;
        }
    
        mainWindow = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            "AutoKeyPresserClass",
            "Auto Key Presser",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 600, 450,
            nullptr,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
    
        if (!mainWindow) {
            MessageBoxA(nullptr, "Failed to create main window.", "Error", MB_ICONERROR);
            return;
        }
    
        listBox = CreateWindow("LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            12, 12, 560, 300,
            mainWindow, nullptr, GetModuleHandle(nullptr), nullptr);
    
        addButton = CreateWindow("BUTTON", "Add Bind",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 330, 125, 37,
            mainWindow, reinterpret_cast<HMENU>(1), GetModuleHandle(nullptr), nullptr);
    
        editButton = CreateWindow("BUTTON", "Edit",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            150, 330, 125, 37,
            mainWindow, reinterpret_cast<HMENU>(2), GetModuleHandle(nullptr), nullptr);
    
        removeButton = CreateWindow("BUTTON", "Remove",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            287, 330, 125, 37,
            mainWindow, reinterpret_cast<HMENU>(3), GetModuleHandle(nullptr), nullptr);
    
        ShowWindow(mainWindow, SW_SHOW);
        UpdateWindow(mainWindow);
    }
    
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case 1:
                        CreateBindDialog(hwnd, nullptr);
                        break;
                    case 2: {
                        int selectedIndex = static_cast<int>(SendMessage(listBox, LB_GETCURSEL, 0, 0));
                        if (selectedIndex != LB_ERR && selectedIndex < static_cast<int>(keyBinds.size())) {
                            CreateBindDialog(hwnd, &keyBinds[selectedIndex]);
                        } else {
                            MessageBoxA(hwnd, "Please select a keybind to edit.",
                                "No Selection", MB_OK | MB_ICONINFORMATION);
                        }
                        break;
                    }
                    case 3: {
                        int selectedIndex = static_cast<int>(SendMessage(listBox, LB_GETCURSEL, 0, 0));
                        if (selectedIndex != LB_ERR && selectedIndex < static_cast<int>(keyBinds.size())) {
                            if (MessageBoxA(hwnd, "Are you sure you want to remove this keybind?",
                                "Confirm Remove", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                keyBinds.erase(keyBinds.begin() + selectedIndex);
                                UpdateBindsList();
                            }
                        } else {
                            MessageBoxA(hwnd, "Please select a keybind to remove.",
                                "No Selection", MB_OK | MB_ICONINFORMATION);
                        }
                        break;
                    }
                    default:
                        return DefWindowProc(hwnd, uMsg, wParam, lParam);
                }
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }
    
    LRESULT CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case 106: {
                        KeyBind* existingBind = reinterpret_cast<KeyBind*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                        KeyBind newBind = KeyBind();
                        char buffer[256];
                        GetDlgItemTextA(hwnd, 101, buffer, sizeof(buffer));
                        newBind.key = StringToVK(buffer);
                        GetDlgItemTextA(hwnd, 102, buffer, sizeof(buffer));
                        newBind.triggerKey = StringToVK(buffer);
                        newBind.isToggle = (IsDlgButtonChecked(hwnd, 103) == BST_CHECKED);
    
                        GetDlgItemTextA(hwnd, 104, buffer, sizeof(buffer));
                        char* end;
                        long pressDelay = std::strtol(buffer, &end, 10);
                        if (*end != '\0') {
                            pressDelay = 50;
                        }
                        newBind.pressDelay = static_cast<int>(pressDelay);
    
                        GetDlgItemTextA(hwnd, 105, buffer, sizeof(buffer));
                        long unpressDelay = std::strtol(buffer, &end, 10);
                        if (*end != '\0') {
                            unpressDelay = 50;
                        }
                        newBind.unpressDelay = static_cast<int>(unpressDelay);
                        
                        newBind.isEnabled = false;
                        newBind.wasKeyPressed = false;
    
                        if (newBind.key == 0 || newBind.triggerKey == 0) {
                            MessageBoxA(hwnd, "Invalid key or trigger key.", "Error", MB_OK | MB_ICONERROR);
                            break;
                        }
    
                        if (existingBind) {
                            *existingBind = newBind;
                        } else {
                            keyBinds.emplace_back(newBind);
                        }
                        UpdateBindsList();
                        DestroyWindow(hwnd);
                        break;
                    }
                    case 107:
                        DestroyWindow(hwnd);
                        break;
                    default:
                        return DefWindowProc(hwnd, uMsg, wParam, lParam);
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                break;
            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }
    
    void ProcessKeyBinds(std::atomic<bool>& running) {
        while (running) {
            DWORD cs2ProcessId = GetCS2ProcessId();
            if (cs2ProcessId != 0) {
                bool yPressed = (GetAsyncKeyState('Y') & 0x8000) != 0;
                bool uPressed = (GetAsyncKeyState('U') & 0x8000) != 0;
                bool escPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                bool enterPressed = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    
                if (!bindsDisabled) {
                    if (yPressed || uPressed) {
                        bindsDisabled = true;
                        disableStartTime = std::chrono::steady_clock::now();
                    }
                }
    
                if (bindsDisabled) {
                    if (escPressed || enterPressed) {
                        bindsDisabled = false;
                    } else {
                        auto now = std::chrono::steady_clock::now();
                        if (now - disableStartTime >= disableDuration) {
                            bindsDisabled = false;
                        }
                    }
                } else {
                    for (auto& bind : keyBinds) {
                        auto keyPressed = (GetAsyncKeyState(bind.triggerKey) & 0x8000) != 0;
                        auto now = std::chrono::steady_clock::now();
    
                        if (bind.isToggle) {
                            if (keyPressed && !bind.wasKeyPressed) {
                                if (now - bind.lastToggleTime > std::chrono::milliseconds(100)) {
                                    bind.isEnabled = !bind.isEnabled;
                                    bind.lastToggleTime = now;
                                }
                                bind.wasKeyPressed = true;
                            }
                            if (!keyPressed) {
                                bind.wasKeyPressed = false;
                            }
                        } else {
                            bind.isEnabled = keyPressed;
                        }
    
                        if (bind.isEnabled) {
                            HWND cs2Window = FindWindowA("SDL_app", "Counter-Strike 2");
                            if (cs2Window) {
                                PostMessage(cs2Window, WM_KEYDOWN, bind.key, 0);
                                
                                auto pressStart = std::chrono::steady_clock::now();
                                auto pressEnd = pressStart + std::chrono::milliseconds(bind.pressDelay);
                                while (std::chrono::steady_clock::now() < pressEnd && running) {
                                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                                }
                                
                                PostMessage(cs2Window, WM_KEYUP, bind.key, 0);
                                
                                auto unpressStart = std::chrono::steady_clock::now();
                                auto unpressEnd = unpressStart + std::chrono::milliseconds(bind.unpressDelay);
                                while (std::chrono::steady_clock::now() < unpressEnd && running) {
                                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                                }
                            }
                        }
                    }
                }
            } else {
                for (auto& bind : keyBinds) {
                    if (bind.isToggle) {
                        bind.wasKeyPressed = false;
                    } else {
                        bind.isEnabled = false;
                    }
                }
                bindsDisabled = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    int main() {
        BOOL isAdmin = FALSE;
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation;
            DWORD size;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                isAdmin = elevation.TokenIsElevated;
            }
            CloseHandle(token);
        }
    
        if (!isAdmin) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(nullptr, path, MAX_PATH)) {
                SHELLEXECUTEINFOW sei = { sizeof(sei) };
                sei.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI;
                sei.lpVerb = L"runas";
                sei.lpFile = path;
                sei.nShow = SW_NORMAL;
    
                if (!ShellExecuteExW(&sei)) {
                    MessageBoxA(nullptr, "This program requires administrator privileges.", "Error", MB_ICONERROR);
                }
                return 0;
            }
        }
        CreateMainWindow();
    
        MSG msg;
        std::atomic<bool> running(true);
        std::thread keyProcessThread(ProcessKeyBinds, std::ref(running));
        keyProcessThread.detach();
    
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    
        running = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 0;
    }
