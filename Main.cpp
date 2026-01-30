#include "KindroidBot.h"
#include <shellapi.h>

// Dark mode colors
#define DARK_BG RGB(30, 30, 30)
#define DARK_BG_EDIT RGB(45, 45, 45)
#define DARK_TEXT RGB(220, 220, 220)
#define DARK_ACCENT RGB(0, 122, 204)

// Timer IDs
#define IDT_ANNOUNCE 2001

// Global variables
HINSTANCE g_hInstance = NULL;
HWND g_hwndMain = NULL;
BotConfig g_config;
DiscordBot* g_bot = nullptr;
TwitchBot* g_twitchBot = nullptr;
KindroidAPI* g_kindroid = nullptr;
std::vector<BotConfig> g_profiles;
std::string g_currentProfileName;
bool g_debugMode = false;

// Dark mode brushes
HBRUSH g_hBrushDarkBg = NULL;
HBRUSH g_hBrushDarkEdit = NULL;

// Tab control subclass
WNDPROC g_origTabProc = NULL;

LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hBrush = CreateSolidBrush(DARK_BG);
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);
            return 1;
        }
        case WM_PAINT: {
            // Let default draw first, then we paint over the background
            LRESULT result = CallWindowProc(g_origTabProc, hwnd, msg, wParam, lParam);
            
            // Now paint our dark background on the display area
            HDC hdc = GetDC(hwnd);
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            // Get display area rect
            RECT displayRect = rc;
            TabCtrl_AdjustRect(hwnd, FALSE, &displayRect);
            
            // Fill display area background
            HBRUSH hBrush = CreateSolidBrush(DARK_BG);
            FillRect(hdc, &displayRect, hBrush);
            DeleteObject(hBrush);
            
            // Draw border around display area
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            
            // Left, bottom, right edges
            MoveToEx(hdc, rc.left, displayRect.top, NULL);
            LineTo(hdc, rc.left, rc.bottom - 1);
            LineTo(hdc, rc.right - 1, rc.bottom - 1);
            LineTo(hdc, rc.right - 1, displayRect.top);
            
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
            
            ReleaseDC(hwnd, hdc);
            return result;
        }
    }
    return CallWindowProc(g_origTabProc, hwnd, msg, wParam, lParam);
}

// Current tab
int g_currentTab = 0; // 0=Kindroid, 1=Discord, 2=Twitch, 3=Announce

// Control handles
HWND g_hwndTabControl = NULL;
HWND g_hwndProfileCombo = NULL;
HWND g_hwndProfileName = NULL;
HWND g_hwndDiscordToken = NULL;
HWND g_hwndApiKey = NULL;
HWND g_hwndAiId = NULL;
HWND g_hwndBaseUrl = NULL;
HWND g_hwndConsole = NULL;
HWND g_hwndStartBtn = NULL;
HWND g_hwndStopBtn = NULL;
HWND g_hwndSaveProfileBtn = NULL;
HWND g_hwndDeleteProfileBtn = NULL;
HWND g_hwndNewProfileBtn = NULL;
HWND g_hwndDebugCheck = NULL;
HWND g_hwndDiscordEnable = NULL;
HWND g_hwndTwitchEnable = NULL;
HWND g_hwndTwitchUsername = NULL;
HWND g_hwndTwitchOAuth = NULL;
HWND g_hwndTwitchChannel = NULL;
HWND g_hwndPersonaName = NULL;
HWND g_hwndDirectInput = NULL;
HWND g_hwndSendBtn = NULL;

// Announcement controls
HWND g_hwndAnnounceMsg = NULL;
HWND g_hwndAnnounceChannel = NULL;
HWND g_hwndAnnounceHours = NULL;
HWND g_hwndAnnounceMins = NULL;
HWND g_hwndAnnounceDiscord = NULL;
HWND g_hwndAnnounceTwitch = NULL;
HWND g_hwndHoursSpin = NULL;
HWND g_hwndMinsSpin = NULL;

// Section control arrays for show/hide
std::vector<HWND> g_kindroidControls;
std::vector<HWND> g_discordControls;
std::vector<HWND> g_twitchControls;
std::vector<HWND> g_announceControls;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    
    // Initialize common controls (including up-down spinners)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&icex);
    
    // Register window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"KindroidDiscordBotClass";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    
    // Fallback to default if custom icon not found
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Create main window with dark mode
    g_hwndMain = CreateWindowEx(
        0,
        L"KindroidDiscordBotClass",
        L"Kindroid API/Bot Server Manager by -=AtlasRedux=-     V1.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 620,
        NULL, NULL, hInstance, NULL
    );
    
    if (g_hwndMain == NULL) {
        MessageBox(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}

// Forward declarations
void OnSendDirect(HWND hwnd);
void OnTabChanged(HWND hwnd);
void OnAnnounceTimer(HWND hwnd);
void OnAnnounceNow(HWND hwnd);
void UpdateTabVisibility();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            // Set console output to UTF-8
            SetConsoleOutputCP(CP_UTF8);
            
            CreateGUI(hwnd);
            
            // Load profiles
            g_profiles = ProfileManager::loadAllProfiles();
            RefreshProfileCombo(hwnd);
            
            // If profiles exist, select the first one
            if (!g_profiles.empty()) {
                SendMessage(g_hwndProfileCombo, CB_SETCURSEL, 0, 0);
                OnProfileSelected(hwnd);
                AppendConsoleText(hwnd, "[INFO] Loaded " + std::to_string(g_profiles.size()) + " profile(s)\n");
            } else {
                // Try loading legacy config.json
                if (ConfigManager::loadConfig(g_config)) {
                    if (g_config.profileName.empty()) {
                        g_config.profileName = "Default";
                    }
                    LoadProfileToGUI(hwnd, g_config);
                    AppendConsoleText(hwnd, "[INFO] Loaded legacy configuration\n");
                } else {
                    AppendConsoleText(hwnd, "[INFO] No profiles found. Create a new profile to get started.\n");
                }
            }
            break;
        
        // Dark mode color handling
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_BG);
            return (LRESULT)g_hBrushDarkBg;
        }
        
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_BG_EDIT);
            return (LRESULT)g_hBrushDarkEdit;
        }
        
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_BG);
            return (LRESULT)g_hBrushDarkBg;
        }
        
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_BG_EDIT);
            return (LRESULT)g_hBrushDarkEdit;
        }
        
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_hBrushDarkBg);
            return 1;
        }
            
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlID == IDC_TAB_CONTROL) {
                // Dark mode tab drawing
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                
                int tabIndex = dis->itemID;
                bool isSelected = (TabCtrl_GetCurSel(g_hwndTabControl) == tabIndex);
                
                // Background - selected is same as panel, unselected is darker
                HBRUSH hBrush = CreateSolidBrush(isSelected ? DARK_BG : RGB(50, 50, 50));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                
                // Border - lighter color for visibility
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                
                // Draw left, top, right borders
                MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
                LineTo(hdc, rc.left, rc.top);
                LineTo(hdc, rc.right - 1, rc.top);
                LineTo(hdc, rc.right - 1, rc.bottom - 1);
                
                if (!isSelected) {
                    // Draw bottom border for unselected tabs
                    LineTo(hdc, rc.left, rc.bottom - 1);
                }
                
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
                
                // Get tab text
                wchar_t text[64] = {0};
                TCITEMW tci = {0};
                tci.mask = TCIF_TEXT;
                tci.pszText = text;
                tci.cchTextMax = 64;
                TabCtrl_GetItem(g_hwndTabControl, tabIndex, &tci);
                
                // Draw text - white for selected, light gray for unselected
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(170, 170, 170));
                rc.top += 4;
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_TOP);
                
                return TRUE;
            }
            break;
        }
            
        case WM_NOTIFY: {
            NMHDR* nmhdr = (NMHDR*)lParam;
            if (nmhdr->idFrom == IDC_TAB_CONTROL && nmhdr->code == TCN_SELCHANGE) {
                OnTabChanged(hwnd);
            }
            break;
        }
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_PROFILE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        OnProfileSelected(hwnd);
                    }
                    break;
                case IDC_DEBUG_CHECK:
                    g_debugMode = (SendMessage(g_hwndDebugCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    break;
                case IDC_START_BTN:
                    OnStartBot(hwnd);
                    break;
                case IDC_STOP_BTN:
                    OnStopBot(hwnd);
                    break;
                case IDC_ANNOUNCE_NOW:
                    OnAnnounceNow(hwnd);
                    break;
                case IDC_SEND_BTN:
                    OnSendDirect(hwnd);
                    break;
                case IDC_SAVE_PROFILE_BTN:
                    OnSaveProfile(hwnd);
                    break;
                case IDC_DELETE_PROFILE_BTN:
                    OnDeleteProfile(hwnd);
                    break;
                case IDC_NEW_PROFILE_BTN:
                    OnNewProfile(hwnd);
                    break;
                case IDC_SAVE_BTN:
                    OnSaveConfig(hwnd);
                    break;
                case IDC_LOAD_BTN:
                    OnLoadConfig(hwnd);
                    break;
                case IDC_OPEN_LOG:
                    ShellExecuteA(NULL, "open", "log.txt", NULL, NULL, SW_SHOWNORMAL);
                    break;
            }
            break;
        
        case WM_TIMER:
            if (wParam == IDT_ANNOUNCE) {
                OnAnnounceTimer(hwnd);
            }
            break;
            
        case WM_USER + 100: {
            // Thread-safe console logging
            char* message = (char*)lParam;
            if (message) {
                AppendConsoleText(hwnd, message);
                free(message);
            }
            break;
        }
            
        case WM_CLOSE:
            if ((g_bot && g_bot->isRunning()) || (g_twitchBot && g_twitchBot->isRunning())) {
                int result = MessageBox(hwnd, 
                    L"Bot is still running. Stop it before closing?", 
                    L"Confirm Exit", 
                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (result == IDYES) {
                    OnStopBot(hwnd);
                    DestroyWindow(hwnd);
                } else if (result == IDNO) {
                    DestroyWindow(hwnd);
                }
                // IDCANCEL: do nothing
            } else {
                DestroyWindow(hwnd);
            }
            break;
            
        case WM_DESTROY:
            if (g_bot) {
                delete g_bot;
                g_bot = nullptr;
            }
            if (g_twitchBot) {
                delete g_twitchBot;
                g_twitchBot = nullptr;
            }
            if (g_kindroid) {
                delete g_kindroid;
                g_kindroid = nullptr;
            }
            // Cleanup dark mode brushes
            if (g_hBrushDarkBg) DeleteObject(g_hBrushDarkBg);
            if (g_hBrushDarkEdit) DeleteObject(g_hBrushDarkEdit);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void CreateGUI(HWND hwnd) {
    // Create dark mode brushes
    g_hBrushDarkBg = CreateSolidBrush(DARK_BG);
    g_hBrushDarkEdit = CreateSolidBrush(DARK_BG_EDIT);
    
    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    
    HFONT hFontBold = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    
    int y = 10;
    HWND hCtrl;
    
    // ========== Profile Selection Section (always visible) ==========
    hCtrl = CreateWindowW(L"STATIC", L"Profile Manager", WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 560, 20, hwnd, NULL, g_hInstance, NULL);
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 25;
    
    CreateWindowW(L"STATIC", L"Select Profile:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_hwndProfileCombo = CreateWindowW(L"COMBOBOX", L"", 
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        120, y - 2, 280, 200, hwnd, (HMENU)IDC_PROFILE_COMBO, g_hInstance, NULL);
    SendMessage(g_hwndProfileCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    g_hwndNewProfileBtn = CreateWindowW(L"BUTTON", L"New", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        410, y - 2, 55, 24, hwnd, (HMENU)IDC_NEW_PROFILE_BTN, g_hInstance, NULL);
    SendMessage(g_hwndNewProfileBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    g_hwndDeleteProfileBtn = CreateWindowW(L"BUTTON", L"Delete", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        470, y - 2, 55, 24, hwnd, (HMENU)IDC_DELETE_PROFILE_BTN, g_hInstance, NULL);
    SendMessage(g_hwndDeleteProfileBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    g_hwndSaveProfileBtn = CreateWindowW(L"BUTTON", L"Save", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        530, y - 2, 50, 24, hwnd, (HMENU)IDC_SAVE_PROFILE_BTN, g_hInstance, NULL);
    SendMessage(g_hwndSaveProfileBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;
    
    hCtrl = CreateWindowW(L"STATIC", L"Profile Name:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_hwndProfileName = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        120, y, 460, 22, hwnd, (HMENU)IDC_PROFILE_NAME, g_hInstance, NULL);
    SendMessage(g_hwndProfileName, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;
    
    // Separator
    CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        10, y, 570, 2, hwnd, NULL, g_hInstance, NULL);
    y += 8;
    
    // ========== Enable Checkboxes (above tabs) ==========
    hCtrl = CreateWindowW(L"STATIC", L"Enable:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 50, 20, hwnd, NULL, g_hInstance, NULL);
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    g_hwndDiscordEnable = CreateWindowW(L"BUTTON", L"Discord Bot", 
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        65, y, 100, 20, hwnd, (HMENU)IDC_DISCORD_ENABLE, g_hInstance, NULL);
    SendMessage(g_hwndDiscordEnable, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hwndDiscordEnable, BM_SETCHECK, BST_CHECKED, 0);
    
    g_hwndTwitchEnable = CreateWindowW(L"BUTTON", L"Twitch Bot", 
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        175, y, 100, 20, hwnd, (HMENU)IDC_TWITCH_ENABLE, g_hInstance, NULL);
    SendMessage(g_hwndTwitchEnable, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 28;
    
    // ========== Tab Control (owner-drawn for dark mode) ==========
    g_hwndTabControl = CreateWindowW(WC_TABCONTROLW, L"",
        WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | TCS_TABS | TCS_OWNERDRAWFIXED,
        10, y, 570, 175, hwnd, (HMENU)IDC_TAB_CONTROL, g_hInstance, NULL);
    SendMessage(g_hwndTabControl, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Subclass the tab control for dark mode background
    g_origTabProc = (WNDPROC)SetWindowLongPtr(g_hwndTabControl, GWLP_WNDPROC, (LONG_PTR)TabSubclassProc);
    
    // Add tabs
    TCITEMW tie = {0};
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)L"Kindroid API";
    TabCtrl_InsertItem(g_hwndTabControl, 0, &tie);
    tie.pszText = (LPWSTR)L"Discord";
    TabCtrl_InsertItem(g_hwndTabControl, 1, &tie);
    tie.pszText = (LPWSTR)L"Twitch";
    TabCtrl_InsertItem(g_hwndTabControl, 2, &tie);
    tie.pszText = (LPWSTR)L"Announcements";
    TabCtrl_InsertItem(g_hwndTabControl, 3, &tie);
    
    // Tab content area starts below tab headers
    int tabY = y + 28;
    int tabX = 20;
    
    // ========== Kindroid API Configuration (Tab 0) ==========
    hCtrl = CreateWindowW(L"STATIC", L"API Key:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        tabX, tabY, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_kindroidControls.push_back(hCtrl);
    g_hwndApiKey = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY, 440, 22, hwnd, (HMENU)IDC_API_KEY, g_hInstance, NULL);
    SendMessage(g_hwndApiKey, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_kindroidControls.push_back(g_hwndApiKey);
    
    hCtrl = CreateWindowW(L"STATIC", L"AI ID:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        tabX, tabY + 28, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_kindroidControls.push_back(hCtrl);
    g_hwndAiId = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY + 28, 440, 22, hwnd, (HMENU)IDC_AI_ID, g_hInstance, NULL);
    SendMessage(g_hwndAiId, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_kindroidControls.push_back(g_hwndAiId);
    
    hCtrl = CreateWindowW(L"STATIC", L"Base URL:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        tabX, tabY + 56, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_kindroidControls.push_back(hCtrl);
    g_hwndBaseUrl = CreateWindowW(L"EDIT", L"https://api.kindroid.ai/v1", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY + 56, 440, 22, hwnd, (HMENU)IDC_BASE_URL, g_hInstance, NULL);
    SendMessage(g_hwndBaseUrl, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_kindroidControls.push_back(g_hwndBaseUrl);
    
    hCtrl = CreateWindowW(L"STATIC", L"Persona Name:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        tabX, tabY + 84, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_kindroidControls.push_back(hCtrl);
    g_hwndPersonaName = CreateWindowW(L"EDIT", L"User", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY + 84, 200, 22, hwnd, (HMENU)IDC_PERSONA_NAME, g_hInstance, NULL);
    SendMessage(g_hwndPersonaName, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_kindroidControls.push_back(g_hwndPersonaName);
    
    // ========== Discord Configuration (Tab 1) ==========
    hCtrl = CreateWindowW(L"STATIC", L"Discord Token:", WS_CHILD | SS_LEFT,
        tabX, tabY, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_discordControls.push_back(hCtrl);
    g_hwndDiscordToken = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY, 440, 22, hwnd, (HMENU)IDC_DISCORD_TOKEN, g_hInstance, NULL);
    SendMessage(g_hwndDiscordToken, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_discordControls.push_back(g_hwndDiscordToken);
    
    // ========== Twitch Configuration (Tab 2) ==========
    hCtrl = CreateWindowW(L"STATIC", L"Bot Username:", WS_CHILD | SS_LEFT,
        tabX, tabY, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_twitchControls.push_back(hCtrl);
    g_hwndTwitchUsername = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY, 180, 22, hwnd, (HMENU)IDC_TWITCH_USERNAME, g_hInstance, NULL);
    SendMessage(g_hwndTwitchUsername, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_twitchControls.push_back(g_hwndTwitchUsername);
    
    hCtrl = CreateWindowW(L"STATIC", L"Channel:", WS_CHILD | SS_LEFT,
        tabX + 300, tabY, 60, 20, hwnd, NULL, g_hInstance, NULL);
    g_twitchControls.push_back(hCtrl);
    g_hwndTwitchChannel = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 360, tabY, 180, 22, hwnd, (HMENU)IDC_TWITCH_CHANNEL, g_hInstance, NULL);
    SendMessage(g_hwndTwitchChannel, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_twitchControls.push_back(g_hwndTwitchChannel);
    
    hCtrl = CreateWindowW(L"STATIC", L"OAuth Token:", WS_CHILD | SS_LEFT,
        tabX, tabY + 28, 100, 20, hwnd, NULL, g_hInstance, NULL);
    g_twitchControls.push_back(hCtrl);
    g_hwndTwitchOAuth = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 100, tabY + 28, 440, 22, hwnd, (HMENU)IDC_TWITCH_OAUTH, g_hInstance, NULL);
    SendMessage(g_hwndTwitchOAuth, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_twitchControls.push_back(g_hwndTwitchOAuth);
    
    // ========== Announcements (Tab 3) ==========
    hCtrl = CreateWindowW(L"STATIC", L"Message:", WS_CHILD | SS_LEFT,
        tabX, tabY, 70, 20, hwnd, NULL, g_hInstance, NULL);
    g_announceControls.push_back(hCtrl);
    g_hwndAnnounceMsg = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 75, tabY, 465, 22, hwnd, (HMENU)IDC_ANNOUNCE_MSG, g_hInstance, NULL);
    SendMessage(g_hwndAnnounceMsg, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(g_hwndAnnounceMsg);
    
    hCtrl = CreateWindowW(L"STATIC", L"Channel ID:", WS_CHILD | SS_LEFT,
        tabX, tabY + 28, 75, 20, hwnd, NULL, g_hInstance, NULL);
    g_announceControls.push_back(hCtrl);
    g_hwndAnnounceChannel = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        tabX + 75, tabY + 28, 150, 22, hwnd, (HMENU)IDC_ANNOUNCE_CHANNEL, g_hInstance, NULL);
    SendMessage(g_hwndAnnounceChannel, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(g_hwndAnnounceChannel);
    
    hCtrl = CreateWindowW(L"STATIC", L"(Discord, right-click > Copy ID)", WS_CHILD | SS_LEFT,
        tabX + 230, tabY + 28, 220, 20, hwnd, NULL, g_hInstance, NULL);
    g_announceControls.push_back(hCtrl);
    
    hCtrl = CreateWindowW(L"STATIC", L"Interval:", WS_CHILD | SS_LEFT,
        tabX, tabY + 56, 55, 20, hwnd, NULL, g_hInstance, NULL);
    g_announceControls.push_back(hCtrl);
    
    g_hwndAnnounceHours = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER,
        tabX + 60, tabY + 56, 35, 22, hwnd, (HMENU)IDC_ANNOUNCE_HOURS, g_hInstance, NULL);
    SendMessage(g_hwndAnnounceHours, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(g_hwndAnnounceHours);
    
    g_hwndHoursSpin = CreateWindowW(UPDOWN_CLASSW, L"", 
        WS_CHILD | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
        0, 0, 0, 0, hwnd, (HMENU)IDC_HOURS_SPIN, g_hInstance, NULL);
    SendMessage(g_hwndHoursSpin, UDM_SETBUDDY, (WPARAM)g_hwndAnnounceHours, 0);
    SendMessage(g_hwndHoursSpin, UDM_SETRANGE, 0, MAKELPARAM(23, 0));
    SendMessage(g_hwndHoursSpin, UDM_SETPOS, 0, 0);
    g_announceControls.push_back(g_hwndHoursSpin);
    
    hCtrl = CreateWindowW(L"STATIC", L"hr", WS_CHILD | SS_LEFT,
        tabX + 100, tabY + 58, 18, 20, hwnd, NULL, g_hInstance, NULL);
    g_announceControls.push_back(hCtrl);
    
    g_hwndAnnounceMins = CreateWindowW(L"EDIT", L"30", WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER,
        tabX + 120, tabY + 56, 35, 22, hwnd, (HMENU)IDC_ANNOUNCE_MINS, g_hInstance, NULL);
    SendMessage(g_hwndAnnounceMins, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(g_hwndAnnounceMins);
    
    g_hwndMinsSpin = CreateWindowW(UPDOWN_CLASSW, L"", 
        WS_CHILD | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
        0, 0, 0, 0, hwnd, (HMENU)IDC_MINS_SPIN, g_hInstance, NULL);
    SendMessage(g_hwndMinsSpin, UDM_SETBUDDY, (WPARAM)g_hwndAnnounceMins, 0);
    SendMessage(g_hwndMinsSpin, UDM_SETRANGE, 0, MAKELPARAM(59, 0));
    SendMessage(g_hwndMinsSpin, UDM_SETPOS, 0, 30);
    g_announceControls.push_back(g_hwndMinsSpin);
    
    hCtrl = CreateWindowW(L"STATIC", L"min", WS_CHILD | SS_LEFT,
        tabX + 160, tabY + 58, 25, 20, hwnd, NULL, g_hInstance, NULL);
    g_announceControls.push_back(hCtrl);
    
    g_hwndAnnounceDiscord = CreateWindowW(L"BUTTON", L"Discord", 
        WS_CHILD | BS_AUTOCHECKBOX,
        tabX + 195, tabY + 56, 70, 20, hwnd, (HMENU)IDC_ANNOUNCE_DISCORD, g_hInstance, NULL);
    SendMessage(g_hwndAnnounceDiscord, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(g_hwndAnnounceDiscord);
    
    g_hwndAnnounceTwitch = CreateWindowW(L"BUTTON", L"Twitch", 
        WS_CHILD | BS_AUTOCHECKBOX,
        tabX + 275, tabY + 56, 70, 20, hwnd, (HMENU)IDC_ANNOUNCE_TWITCH, g_hInstance, NULL);
    SendMessage(g_hwndAnnounceTwitch, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(g_hwndAnnounceTwitch);
    
    hCtrl = CreateWindowW(L"BUTTON", L"Send Now", WS_CHILD | BS_PUSHBUTTON,
        tabX + 360, tabY + 54, 75, 24, hwnd, (HMENU)IDC_ANNOUNCE_NOW, g_hInstance, NULL);
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_announceControls.push_back(hCtrl);
    
    // Move y past the tab control (tab height 175 + enable checkboxes 28)
    y += 185;
    
    // Separator
    CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        10, y, 570, 2, hwnd, NULL, g_hInstance, NULL);
    y += 10;
    
    // ========== Bot Control Buttons ==========
    g_hwndStartBtn = CreateWindowW(L"BUTTON", L"Start Server", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 110, 30, hwnd, (HMENU)IDC_START_BTN, g_hInstance, NULL);
    SendMessage(g_hwndStartBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    g_hwndStopBtn = CreateWindowW(L"BUTTON", L"Stop Server", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
        130, y, 110, 30, hwnd, (HMENU)IDC_STOP_BTN, g_hInstance, NULL);
    SendMessage(g_hwndStopBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    CreateWindowW(L"BUTTON", L"Export...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        400, y, 80, 30, hwnd, (HMENU)IDC_SAVE_BTN, g_hInstance, NULL);
    
    CreateWindowW(L"BUTTON", L"Import...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        490, y, 80, 30, hwnd, (HMENU)IDC_LOAD_BTN, g_hInstance, NULL);
    y += 35;
    
    // ========== Console ==========
    hCtrl = CreateWindowW(L"STATIC", L"Console Log:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 100, 20, hwnd, NULL, g_hInstance, NULL);
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    
    CreateWindowW(L"BUTTON", L"Open Log", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, y - 2, 70, 22, hwnd, (HMENU)IDC_OPEN_LOG, g_hInstance, NULL);
    
    g_hwndDebugCheck = CreateWindowW(L"BUTTON", L"Show Debug", 
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        420, y, 100, 20, hwnd, (HMENU)IDC_DEBUG_CHECK, g_hInstance, NULL);
    SendMessage(g_hwndDebugCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 22;
    
    g_hwndConsole = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        10, y, 570, 140, hwnd, (HMENU)IDC_CONSOLE, g_hInstance, NULL);
    SendMessage(g_hwndConsole, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 145;
    
    // ========== Direct Input ==========
    hCtrl = CreateWindowW(L"STATIC", L"Direct Message to Kin:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 110, 20, hwnd, NULL, g_hInstance, NULL);
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 22;
    
    g_hwndDirectInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        10, y, 480, 24, hwnd, (HMENU)IDC_DIRECT_INPUT, g_hInstance, NULL);
    SendMessage(g_hwndDirectInput, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    g_hwndSendBtn = CreateWindowW(L"BUTTON", L"Send", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        500, y, 80, 26, hwnd, (HMENU)IDC_SEND_BTN, g_hInstance, NULL);
    SendMessage(g_hwndSendBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Show initial tab
    UpdateTabVisibility();
}


void UpdateTabVisibility() {
    g_currentTab = TabCtrl_GetCurSel(g_hwndTabControl);
    
    // Hide all tab contents first
    for (HWND ctrl : g_kindroidControls) ShowWindow(ctrl, SW_HIDE);
    for (HWND ctrl : g_discordControls) ShowWindow(ctrl, SW_HIDE);
    for (HWND ctrl : g_twitchControls) ShowWindow(ctrl, SW_HIDE);
    for (HWND ctrl : g_announceControls) ShowWindow(ctrl, SW_HIDE);
    
    // Show current tab contents
    switch (g_currentTab) {
        case 0: // Kindroid
            for (HWND ctrl : g_kindroidControls) ShowWindow(ctrl, SW_SHOW);
            break;
        case 1: // Discord
            for (HWND ctrl : g_discordControls) ShowWindow(ctrl, SW_SHOW);
            break;
        case 2: // Twitch
            for (HWND ctrl : g_twitchControls) ShowWindow(ctrl, SW_SHOW);
            break;
        case 3: // Announcements
            for (HWND ctrl : g_announceControls) ShowWindow(ctrl, SW_SHOW);
            break;
    }
}

void OnTabChanged(HWND hwnd) {
    UpdateTabVisibility();
    InvalidateRect(hwnd, NULL, TRUE);
}

void OnAnnounceTimer(HWND hwnd) {
    if (g_config.announceMessage.empty()) return;
    
    AppendConsoleText(hwnd, "[ANNOUNCE] Sending announcement...\n");
    
    if (g_config.announceDiscord && g_bot && g_bot->isRunning()) {
        g_bot->sendAnnouncement(g_config.announceMessage, g_config.announceDiscordChannel);
    }
    
    if (g_config.announceTwitch && g_twitchBot && g_twitchBot->isRunning()) {
        g_twitchBot->sendAnnouncement(g_config.announceMessage);
    }
}

void OnAnnounceNow(HWND hwnd) {
    // Get current values from GUI
    char buffer[512];
    GetWindowTextA(g_hwndAnnounceMsg, buffer, 512);
    std::string message = buffer;
    
    if (message.empty()) {
        MessageBoxA(hwnd, "Please enter an announcement message!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    GetWindowTextA(g_hwndAnnounceChannel, buffer, 512);
    std::string channelId = buffer;
    
    bool toDiscord = (SendMessage(g_hwndAnnounceDiscord, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool toTwitch = (SendMessage(g_hwndAnnounceTwitch, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    if (!toDiscord && !toTwitch) {
        MessageBoxA(hwnd, "Please select at least one platform (Discord or Twitch)!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    AppendConsoleText(hwnd, "[ANNOUNCE] Sending manual announcement...\n");
    
    if (toDiscord) {
        if (g_bot && g_bot->isRunning()) {
            g_bot->sendAnnouncement(message, channelId);
        } else {
            AppendConsoleText(hwnd, "[ANNOUNCE] Discord bot not running\n");
        }
    }
    
    if (toTwitch) {
        if (g_twitchBot && g_twitchBot->isRunning()) {
            g_twitchBot->sendAnnouncement(message);
        } else {
            AppendConsoleText(hwnd, "[ANNOUNCE] Twitch bot not running\n");
        }
    }
}

void RefreshProfileCombo(HWND hwnd) {
    // Clear existing items
    SendMessage(g_hwndProfileCombo, CB_RESETCONTENT, 0, 0);
    
    // Reload profiles from file
    g_profiles = ProfileManager::loadAllProfiles();
    
    // Add profiles to combo
    for (const auto& profile : g_profiles) {
        std::wstring wName = stringToWstring(profile.profileName);
        SendMessageW(g_hwndProfileCombo, CB_ADDSTRING, 0, (LPARAM)wName.c_str());
    }
    
    // Update delete button state
    EnableWindow(g_hwndDeleteProfileBtn, !g_profiles.empty());
}

void LoadProfileToGUI(HWND hwnd, const BotConfig& profile) {
    SetWindowTextA(g_hwndProfileName, profile.profileName.c_str());
    SetWindowTextA(g_hwndDiscordToken, censorString(profile.discordToken).c_str());
    SetWindowTextA(g_hwndApiKey, censorString(profile.apiKey).c_str());
    SetWindowTextA(g_hwndAiId, profile.aiId.c_str());
    SetWindowTextA(g_hwndBaseUrl, profile.baseUrl.c_str());
    SetWindowTextA(g_hwndPersonaName, profile.personaName.c_str());
    SendMessage(g_hwndDiscordEnable, BM_SETCHECK, profile.discordEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    
    // Twitch settings
    SetWindowTextA(g_hwndTwitchUsername, profile.twitchUsername.c_str());
    SetWindowTextA(g_hwndTwitchOAuth, censorString(profile.twitchOAuth).c_str());
    SetWindowTextA(g_hwndTwitchChannel, profile.twitchChannel.c_str());
    SendMessage(g_hwndTwitchEnable, BM_SETCHECK, profile.twitchEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    
    // Announcement settings
    SetWindowTextA(g_hwndAnnounceMsg, profile.announceMessage.c_str());
    SetWindowTextA(g_hwndAnnounceChannel, profile.announceDiscordChannel.c_str());
    SendMessage(g_hwndHoursSpin, UDM_SETPOS, 0, profile.announceHours);
    SendMessage(g_hwndMinsSpin, UDM_SETPOS, 0, profile.announceMins);
    SendMessage(g_hwndAnnounceDiscord, BM_SETCHECK, profile.announceDiscord ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hwndAnnounceTwitch, BM_SETCHECK, profile.announceTwitch ? BST_CHECKED : BST_UNCHECKED, 0);
    
    // Store the actual values in g_config
    g_config = profile;
    g_currentProfileName = profile.profileName;
}

void OnProfileSelected(HWND hwnd) {
    int sel = (int)SendMessage(g_hwndProfileCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR || sel >= (int)g_profiles.size()) {
        return;
    }
    
    const BotConfig& profile = g_profiles[sel];
    LoadProfileToGUI(hwnd, profile);
    
    AppendConsoleText(hwnd, "[INFO] Selected profile: " + profile.profileName + "\n");
}

void OnSaveProfile(HWND hwnd) {
    char buffer[512];
    
    // Get profile name
    GetWindowTextA(g_hwndProfileName, buffer, 512);
    std::string profileName = buffer;
    
    if (profileName.empty()) {
        MessageBoxA(hwnd, "Please enter a profile name!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Build config from GUI
    BotConfig profile;
    profile.profileName = profileName;
    
    // Get Discord token - check if censored
    GetWindowTextA(g_hwndDiscordToken, buffer, 512);
    std::string tokenFromGUI = buffer;
    if (tokenFromGUI.find('*') != std::string::npos && g_currentProfileName == profileName) {
        // Use stored value
        profile.discordToken = g_config.discordToken;
    } else if (tokenFromGUI.find('*') != std::string::npos) {
        // Find in existing profiles
        BotConfig* existing = ProfileManager::findProfile(g_profiles, profileName);
        if (existing) {
            profile.discordToken = existing->discordToken;
        } else {
            profile.discordToken = tokenFromGUI; // Will fail validation
        }
    } else {
        profile.discordToken = tokenFromGUI;
    }
    
    // Get API key - check if censored
    GetWindowTextA(g_hwndApiKey, buffer, 512);
    std::string apiKeyFromGUI = buffer;
    if (apiKeyFromGUI.find('*') != std::string::npos && g_currentProfileName == profileName) {
        profile.apiKey = g_config.apiKey;
    } else if (apiKeyFromGUI.find('*') != std::string::npos) {
        BotConfig* existing = ProfileManager::findProfile(g_profiles, profileName);
        if (existing) {
            profile.apiKey = existing->apiKey;
        } else {
            profile.apiKey = apiKeyFromGUI;
        }
    } else {
        profile.apiKey = apiKeyFromGUI;
    }
    
    GetWindowTextA(g_hwndAiId, buffer, 512);
    profile.aiId = buffer;
    
    GetWindowTextA(g_hwndBaseUrl, buffer, 512);
    profile.baseUrl = buffer;
    if (profile.baseUrl.empty()) {
        profile.baseUrl = "https://api.kindroid.ai/v1";
    }
    
    GetWindowTextA(g_hwndPersonaName, buffer, 512);
    profile.personaName = buffer;
    if (profile.personaName.empty()) {
        profile.personaName = "User";
    }
    
    profile.discordEnabled = (SendMessage(g_hwndDiscordEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    // Get Twitch settings
    GetWindowTextA(g_hwndTwitchUsername, buffer, 512);
    profile.twitchUsername = buffer;
    
    GetWindowTextA(g_hwndTwitchChannel, buffer, 512);
    profile.twitchChannel = buffer;
    
    GetWindowTextA(g_hwndTwitchOAuth, buffer, 512);
    std::string oauthFromGUI = buffer;
    if (oauthFromGUI.find('*') != std::string::npos && g_currentProfileName == profileName) {
        profile.twitchOAuth = g_config.twitchOAuth;
    } else if (oauthFromGUI.find('*') != std::string::npos) {
        BotConfig* existing = ProfileManager::findProfile(g_profiles, profileName);
        if (existing) {
            profile.twitchOAuth = existing->twitchOAuth;
        } else {
            profile.twitchOAuth = oauthFromGUI;
        }
    } else {
        profile.twitchOAuth = oauthFromGUI;
    }
    
    profile.twitchEnabled = (SendMessage(g_hwndTwitchEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    // Get Announcement settings
    GetWindowTextA(g_hwndAnnounceMsg, buffer, 512);
    profile.announceMessage = buffer;
    GetWindowTextA(g_hwndAnnounceChannel, buffer, 512);
    profile.announceDiscordChannel = buffer;
    profile.announceHours = (int)SendMessage(g_hwndHoursSpin, UDM_GETPOS, 0, 0) & 0xFFFF;
    profile.announceMins = (int)SendMessage(g_hwndMinsSpin, UDM_GETPOS, 0, 0) & 0xFFFF;
    profile.announceDiscord = (SendMessage(g_hwndAnnounceDiscord, BM_GETCHECK, 0, 0) == BST_CHECKED);
    profile.announceTwitch = (SendMessage(g_hwndAnnounceTwitch, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    // Validate
    if (profile.discordToken.empty() || profile.discordToken.find('*') != std::string::npos) {
        MessageBoxA(hwnd, "Please enter a valid Discord token!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    if (profile.apiKey.empty() || profile.apiKey.find('*') != std::string::npos) {
        MessageBoxA(hwnd, "Please enter a valid API key!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    if (profile.aiId.empty()) {
        MessageBoxA(hwnd, "Please enter an AI ID!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Save profile
    if (ProfileManager::addOrUpdateProfile(profile)) {
        g_config = profile;
        g_currentProfileName = profileName;
        
        RefreshProfileCombo(hwnd);
        
        // Select the saved profile in combo
        for (int i = 0; i < (int)g_profiles.size(); i++) {
            if (g_profiles[i].profileName == profileName) {
                SendMessage(g_hwndProfileCombo, CB_SETCURSEL, i, 0);
                break;
            }
        }
        
        AppendConsoleText(hwnd, "[INFO] Profile saved: " + profileName + "\n");
    } else {
        AppendConsoleText(hwnd, "[ERROR] Failed to save profile\n");
    }
}

void OnDeleteProfile(HWND hwnd) {
    int sel = (int)SendMessage(g_hwndProfileCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR || sel >= (int)g_profiles.size()) {
        MessageBoxA(hwnd, "Please select a profile to delete!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    std::string profileName = g_profiles[sel].profileName;
    
    // Confirm deletion
    std::string msg = "Are you sure you want to delete the profile \"" + profileName + "\"?";
    int result = MessageBoxA(hwnd, msg.c_str(), "Confirm Delete", MB_YESNO | MB_ICONQUESTION);
    
    if (result == IDYES) {
        if (ProfileManager::deleteProfile(profileName)) {
            AppendConsoleText(hwnd, "[INFO] Profile deleted: " + profileName + "\n");
            
            RefreshProfileCombo(hwnd);
            
            // Clear the form or select first profile
            if (!g_profiles.empty()) {
                SendMessage(g_hwndProfileCombo, CB_SETCURSEL, 0, 0);
                OnProfileSelected(hwnd);
            } else {
                // Clear form
                SetWindowTextA(g_hwndProfileName, "");
                SetWindowTextA(g_hwndDiscordToken, "");
                SetWindowTextA(g_hwndApiKey, "");
                SetWindowTextA(g_hwndAiId, "");
                SetWindowTextA(g_hwndBaseUrl, "https://api.kindroid.ai/v1");
                g_currentProfileName = "";
            }
        } else {
            AppendConsoleText(hwnd, "[ERROR] Failed to delete profile\n");
        }
    }
}

void OnNewProfile(HWND hwnd) {
    // Clear the form for a new profile
    SetWindowTextA(g_hwndProfileName, "");
    SetWindowTextA(g_hwndDiscordToken, "");
    SetWindowTextA(g_hwndApiKey, "");
    SetWindowTextA(g_hwndAiId, "");
    SetWindowTextA(g_hwndBaseUrl, "https://api.kindroid.ai/v1");
    SetWindowTextA(g_hwndPersonaName, "User");
    
    // Clear Twitch fields
    SetWindowTextA(g_hwndTwitchUsername, "");
    SetWindowTextA(g_hwndTwitchOAuth, "");
    SetWindowTextA(g_hwndTwitchChannel, "");
    SendMessage(g_hwndTwitchEnable, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(g_hwndDiscordEnable, BM_SETCHECK, BST_CHECKED, 0); // Default Discord enabled
    
    // Clear Announcement fields
    SetWindowTextA(g_hwndAnnounceMsg, "");
    SetWindowTextA(g_hwndAnnounceChannel, "");
    SendMessage(g_hwndHoursSpin, UDM_SETPOS, 0, 0);
    SendMessage(g_hwndMinsSpin, UDM_SETPOS, 0, 30);
    SendMessage(g_hwndAnnounceDiscord, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(g_hwndAnnounceTwitch, BM_SETCHECK, BST_UNCHECKED, 0);
    
    // Deselect combo
    SendMessage(g_hwndProfileCombo, CB_SETCURSEL, (WPARAM)-1, 0);
    
    // Clear current profile tracking
    g_currentProfileName = "";
    g_config = BotConfig();
    
    // Focus on profile name field
    SetFocus(g_hwndProfileName);
    
    AppendConsoleText(hwnd, "[INFO] Creating new profile - enter details and click Save\n");
}

void OnSendDirect(HWND hwnd) {
    char buffer[4096];
    
    // Get the message
    GetWindowTextA(g_hwndDirectInput, buffer, 4096);
    std::string message = buffer;
    
    if (message.empty()) {
        return;
    }
    
    // Get persona name
    GetWindowTextA(g_hwndPersonaName, buffer, 512);
    std::string personaName = buffer;
    if (personaName.empty()) {
        personaName = "User";
    }
    
    // Check if we have API credentials
    if (g_config.apiKey.empty() || g_config.aiId.empty()) {
        AppendConsoleText(hwnd, "[ERROR] Please configure Kindroid API Key and AI ID first!\n");
        return;
    }
    
    // Create temp API client if needed
    KindroidAPI* api = g_kindroid;
    bool tempApi = false;
    if (!api) {
        api = new KindroidAPI(g_config.apiKey, g_config.aiId, g_config.baseUrl);
        tempApi = true;
    }
    
    // Send with special context
    std::string context = "Sent directly from Kindroid API/Bot Manager by " + personaName;
    
    AppendConsoleText(hwnd, "[DIRECT] " + personaName + ": " + message + "\n");
    
    // Send in background thread to not freeze UI
    std::thread([hwnd, api, personaName, context, message, tempApi]() {
        std::string response = api->sendMessage(personaName, context, message);
        
        // Post response to console (must be done on main thread)
        std::string logMsg = "[KINDROID] " + response + "\n";
        char* msgCopy = _strdup(logMsg.c_str());
        PostMessageA(hwnd, WM_USER + 100, 0, (LPARAM)msgCopy);
        
        if (tempApi) {
            delete api;
        }
    }).detach();
    
    // Clear input
    SetWindowTextA(g_hwndDirectInput, "");
}

void WriteToLogFile(const std::string& text) {
    FILE* logFile = fopen("log.txt", "a");
    if (logFile) {
        std::string timestamp = getCurrentTimestamp();
        fprintf(logFile, "[%s] %s", timestamp.c_str(), text.c_str());
        fclose(logFile);
    }
}

void AppendConsoleText(HWND hwnd, const std::string& text) {
    if (!g_hwndConsole) return;
    
    std::string timestamp = getCurrentTimestamp();
    std::string logLine = "[" + timestamp + "] " + text;
    
    // Always write to log file (including DEBUG)
    WriteToLogFile(text);
    
    // Convert UTF-8 string to wide string for proper display
    std::wstring wLogLine = stringToWstring(logLine);
    
    int len = GetWindowTextLengthW(g_hwndConsole);
    SendMessageW(g_hwndConsole, EM_SETSEL, len, len);
    SendMessageW(g_hwndConsole, EM_REPLACESEL, FALSE, (LPARAM)wLogLine.c_str());
    SendMessageW(g_hwndConsole, EM_SCROLLCARET, 0, 0);
}

void OnStartBot(HWND hwnd) {
    // Get values from GUI - but only if they're not censored
    char buffer[512];
    
    GetWindowTextA(g_hwndDiscordToken, buffer, 512);
    std::string tokenFromGUI = buffer;
    if (tokenFromGUI.find('*') != std::string::npos) {
        // Using stored token from config
    } else {
        g_config.discordToken = tokenFromGUI;
    }
    
    GetWindowTextA(g_hwndApiKey, buffer, 512);
    std::string apiKeyFromGUI = buffer;
    if (apiKeyFromGUI.find('*') != std::string::npos) {
        // Using stored API key from config
    } else {
        g_config.apiKey = apiKeyFromGUI;
    }
    
    GetWindowTextA(g_hwndAiId, buffer, 512);
    g_config.aiId = buffer;
    
    GetWindowTextA(g_hwndBaseUrl, buffer, 512);
    std::string baseUrl = buffer;
    g_config.baseUrl = baseUrl.empty() ? "https://api.kindroid.ai/v1" : baseUrl;
    
    GetWindowTextA(g_hwndProfileName, buffer, 512);
    g_config.profileName = buffer;
    
    // Get Twitch settings
    GetWindowTextA(g_hwndTwitchUsername, buffer, 512);
    g_config.twitchUsername = buffer;
    
    GetWindowTextA(g_hwndTwitchChannel, buffer, 512);
    g_config.twitchChannel = buffer;
    
    GetWindowTextA(g_hwndTwitchOAuth, buffer, 512);
    std::string oauthFromGUI = buffer;
    if (oauthFromGUI.find('*') != std::string::npos) {
        // Using stored OAuth from config
    } else {
        g_config.twitchOAuth = oauthFromGUI;
    }
    
    g_config.twitchEnabled = (SendMessage(g_hwndTwitchEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.discordEnabled = (SendMessage(g_hwndDiscordEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    // Get Announcement settings
    GetWindowTextA(g_hwndAnnounceMsg, buffer, 512);
    g_config.announceMessage = buffer;
    GetWindowTextA(g_hwndAnnounceChannel, buffer, 512);
    g_config.announceDiscordChannel = buffer;
    g_config.announceHours = (int)SendMessage(g_hwndHoursSpin, UDM_GETPOS, 0, 0) & 0xFFFF;
    g_config.announceMins = (int)SendMessage(g_hwndMinsSpin, UDM_GETPOS, 0, 0) & 0xFFFF;
    g_config.announceDiscord = (SendMessage(g_hwndAnnounceDiscord, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.announceTwitch = (SendMessage(g_hwndAnnounceTwitch, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    // Validation - need at least one bot enabled
    if (!g_config.discordEnabled && !g_config.twitchEnabled) {
        MessageBoxA(hwnd, "Please enable at least one bot (Discord or Twitch)!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Validate Kindroid API settings (always required)
    if (g_config.apiKey.empty() || g_config.aiId.empty()) {
        MessageBoxA(hwnd, "Please fill in Kindroid API Key and AI ID!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Validate Discord settings if enabled
    if (g_config.discordEnabled) {
        if (g_config.discordToken.empty()) {
            MessageBoxA(hwnd, "Discord is enabled but missing Discord Token!", "Error", MB_OK | MB_ICONERROR);
            return;
        }
    }
    
    // Validate Twitch settings if enabled
    if (g_config.twitchEnabled) {
        if (g_config.twitchUsername.empty() || g_config.twitchOAuth.empty() || g_config.twitchChannel.empty()) {
            MessageBoxA(hwnd, "Twitch is enabled but missing Username, OAuth, or Channel!", "Error", MB_OK | MB_ICONERROR);
            return;
        }
    }
    
    // Auto-save profile if it has a name
    if (!g_config.profileName.empty()) {
        ProfileManager::addOrUpdateProfile(g_config);
        RefreshProfileCombo(hwnd);
    }
    
    std::string profileInfo = g_config.profileName.empty() ? "unnamed config" : g_config.profileName;
    AppendConsoleText(hwnd, "[INFO] Starting bot with profile: " + profileInfo + "\n");
    
    // Create Kindroid API client
    if (g_kindroid) delete g_kindroid;
    g_kindroid = new KindroidAPI(g_config.apiKey, g_config.aiId, g_config.baseUrl);
    
    // Create and start Discord bot if enabled
    if (g_config.discordEnabled) {
        if (g_bot) delete g_bot;
        g_bot = new DiscordBot(g_config.discordToken, g_kindroid, g_hwndMain);
        g_bot->start();
        AppendConsoleText(hwnd, "[INFO] Discord bot enabled\n");
    }
    
    // Create and start Twitch bot if enabled
    if (g_config.twitchEnabled) {
        if (g_twitchBot) delete g_twitchBot;
        g_twitchBot = new TwitchBot(g_config.twitchUsername, g_config.twitchOAuth, 
                                     g_config.twitchChannel, g_kindroid, g_hwndMain);
        g_twitchBot->start();
        AppendConsoleText(hwnd, "[INFO] Twitch bot enabled for #" + g_config.twitchChannel + "\n");
    }
    
    // Update UI - disable editing while running
    EnableWindow(g_hwndStartBtn, FALSE);
    EnableWindow(g_hwndStopBtn, TRUE);
    EnableWindow(g_hwndProfileCombo, FALSE);
    EnableWindow(g_hwndProfileName, FALSE);
    EnableWindow(g_hwndDiscordEnable, FALSE);
    EnableWindow(g_hwndDiscordToken, FALSE);
    EnableWindow(g_hwndApiKey, FALSE);
    EnableWindow(g_hwndAiId, FALSE);
    EnableWindow(g_hwndBaseUrl, FALSE);
    EnableWindow(g_hwndTwitchEnable, FALSE);
    EnableWindow(g_hwndTwitchUsername, FALSE);
    EnableWindow(g_hwndTwitchOAuth, FALSE);
    EnableWindow(g_hwndTwitchChannel, FALSE);
    EnableWindow(g_hwndSaveProfileBtn, FALSE);
    EnableWindow(g_hwndDeleteProfileBtn, FALSE);
    EnableWindow(g_hwndNewProfileBtn, FALSE);
    EnableWindow(g_hwndAnnounceMsg, FALSE);
    EnableWindow(g_hwndAnnounceHours, FALSE);
    EnableWindow(g_hwndAnnounceMins, FALSE);
    EnableWindow(g_hwndAnnounceDiscord, FALSE);
    EnableWindow(g_hwndAnnounceTwitch, FALSE);
    
    // Start announcement timer if configured
    if (!g_config.announceMessage.empty() && (g_config.announceDiscord || g_config.announceTwitch)) {
        int totalMins = g_config.announceHours * 60 + g_config.announceMins;
        if (totalMins > 0) {
            UINT interval = totalMins * 60 * 1000; // Convert to milliseconds
            SetTimer(hwnd, IDT_ANNOUNCE, interval, NULL);
            AppendConsoleText(hwnd, "[INFO] Announcements enabled every " + 
                std::to_string(g_config.announceHours) + "h " + 
                std::to_string(g_config.announceMins) + "m\n");
        }
    }
    
    AppendConsoleText(hwnd, "[INFO] Bot started successfully\n");
}

void OnStopBot(HWND hwnd) {
    // Stop announcement timer
    KillTimer(hwnd, IDT_ANNOUNCE);
    
    if (g_bot) {
        AppendConsoleText(hwnd, "[INFO] Stopping Discord bot...\n");
        g_bot->stop();
        delete g_bot;
        g_bot = nullptr;
    }
    
    if (g_twitchBot) {
        AppendConsoleText(hwnd, "[INFO] Stopping Twitch bot...\n");
        g_twitchBot->stop();
        delete g_twitchBot;
        g_twitchBot = nullptr;
    }
    
    AppendConsoleText(hwnd, "[INFO] All bots stopped\n");
    
    // Update UI - re-enable editing
    EnableWindow(g_hwndStartBtn, TRUE);
    EnableWindow(g_hwndStopBtn, FALSE);
    EnableWindow(g_hwndProfileCombo, TRUE);
    EnableWindow(g_hwndProfileName, TRUE);
    EnableWindow(g_hwndDiscordEnable, TRUE);
    EnableWindow(g_hwndDiscordToken, TRUE);
    EnableWindow(g_hwndApiKey, TRUE);
    EnableWindow(g_hwndAiId, TRUE);
    EnableWindow(g_hwndBaseUrl, TRUE);
    EnableWindow(g_hwndTwitchEnable, TRUE);
    EnableWindow(g_hwndTwitchUsername, TRUE);
    EnableWindow(g_hwndTwitchOAuth, TRUE);
    EnableWindow(g_hwndTwitchChannel, TRUE);
    EnableWindow(g_hwndSaveProfileBtn, TRUE);
    EnableWindow(g_hwndDeleteProfileBtn, !g_profiles.empty());
    EnableWindow(g_hwndNewProfileBtn, TRUE);
    EnableWindow(g_hwndAnnounceMsg, TRUE);
    EnableWindow(g_hwndAnnounceHours, TRUE);
    EnableWindow(g_hwndAnnounceMins, TRUE);
    EnableWindow(g_hwndAnnounceDiscord, TRUE);
    EnableWindow(g_hwndAnnounceTwitch, TRUE);
}

void OnSaveConfig(HWND hwnd) {
    OPENFILENAMEA ofn = { 0 };
    char fileName[MAX_PATH] = "config.json";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";
    ofn.lpstrTitle = "Export Configuration";
    
    if (GetSaveFileNameA(&ofn)) {
        // Get current values (use stored values for censored fields)
        char buffer[512];
        
        GetWindowTextA(g_hwndProfileName, buffer, 512);
        g_config.profileName = buffer;
        
        GetWindowTextA(g_hwndDiscordToken, buffer, 512);
        if (std::string(buffer).find('*') == std::string::npos) {
            g_config.discordToken = buffer;
        }
        
        GetWindowTextA(g_hwndApiKey, buffer, 512);
        if (std::string(buffer).find('*') == std::string::npos) {
            g_config.apiKey = buffer;
        }
        
        GetWindowTextA(g_hwndAiId, buffer, 512);
        g_config.aiId = buffer;
        
        GetWindowTextA(g_hwndBaseUrl, buffer, 512);
        g_config.baseUrl = buffer;
        
        if (ConfigManager::saveConfig(g_config, fileName)) {
            AppendConsoleText(hwnd, "[INFO] Configuration exported to " + std::string(fileName) + "\n");
        } else {
            AppendConsoleText(hwnd, "[ERROR] Failed to export configuration\n");
        }
    }
}

void OnLoadConfig(HWND hwnd) {
    OPENFILENAMEA ofn = { 0 };
    char fileName[MAX_PATH] = "";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Import Configuration";
    
    if (GetOpenFileNameA(&ofn)) {
        BotConfig importedConfig;
        if (ConfigManager::loadConfig(importedConfig, fileName)) {
            // If no profile name, use filename
            if (importedConfig.profileName.empty()) {
                std::string fn = fileName;
                size_t lastSlash = fn.find_last_of("\\/");
                size_t lastDot = fn.find_last_of(".");
                if (lastSlash != std::string::npos) {
                    fn = fn.substr(lastSlash + 1);
                }
                if (lastDot != std::string::npos && lastDot > 0) {
                    fn = fn.substr(0, lastDot);
                }
                importedConfig.profileName = fn;
            }
            
            LoadProfileToGUI(hwnd, importedConfig);
            AppendConsoleText(hwnd, "[INFO] Configuration imported from " + std::string(fileName) + "\n");
            AppendConsoleText(hwnd, "[INFO] Click 'Save' to add this as a profile\n");
        } else {
            AppendConsoleText(hwnd, "[ERROR] Failed to import configuration\n");
        }
    }
}
