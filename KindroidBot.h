#pragma once

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <utility>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")

// Forward declarations
class DiscordBot;
class KindroidAPI;
class ConfigManager;
class ProfileManager;
class SchannelConnection;

// Schannel SSL functions
struct SchannelContext;
SchannelContext* SchannelCreate(SOCKET sock);
bool SchannelHandshake(SchannelContext* ctx, const char* hostname);
int SchannelSend(SchannelContext* ctx, const void* data, int len);
int SchannelRecv(SchannelContext* ctx, void* buffer, int len);
void SchannelDestroy(SchannelContext* ctx);

// GUI Controls IDs
#define IDC_DISCORD_TOKEN       1001
#define IDC_API_KEY             1002
#define IDC_AI_ID               1003
#define IDC_BASE_URL            1004
#define IDC_START_BTN           1005
#define IDC_STOP_BTN            1006
#define IDC_SAVE_BTN            1007
#define IDC_LOAD_BTN            1008
#define IDC_CONSOLE             1009
#define IDC_PROFILE_COMBO       1010
#define IDC_PROFILE_NAME        1011
#define IDC_SAVE_PROFILE_BTN    1012
#define IDC_DELETE_PROFILE_BTN  1013
#define IDC_NEW_PROFILE_BTN     1014
#define IDC_DEBUG_CHECK         1015
#define IDC_TWITCH_ENABLE       1016
#define IDC_TWITCH_USERNAME     1017
#define IDC_TWITCH_OAUTH        1018
#define IDC_TWITCH_CHANNEL      1019
#define IDC_DISCORD_ENABLE      1020
#define IDC_PERSONA_NAME        1021
#define IDC_DIRECT_INPUT        1022
#define IDC_SEND_BTN            1023
#define IDC_ANNOUNCE_MSG        1024
#define IDC_ANNOUNCE_HOURS      1025
#define IDC_ANNOUNCE_MINS       1026
#define IDC_ANNOUNCE_DISCORD    1027
#define IDC_ANNOUNCE_TWITCH     1028
#define IDC_SECTION_COMBO       1029
#define IDC_HOURS_SPIN          1030
#define IDC_MINS_SPIN           1031
#define IDC_SHOW_KINDROID       1032
#define IDC_SHOW_DISCORD        1033
#define IDC_SHOW_TWITCH         1034
#define IDC_SHOW_ANNOUNCE       1035
#define IDC_ANNOUNCE_NOW        1036
#define IDC_ANNOUNCE_CHANNEL    1037
#define IDC_TAB_CONTROL         1038
#define IDC_OPEN_LOG            1039

// Configuration structure
struct BotConfig {
    std::string profileName;    // Display name for this profile
    std::string discordToken;
    std::string apiKey;
    std::string aiId;
    std::string baseUrl;
    std::string personaName;    // User's persona name for direct messages
    bool debugMode;             // Show debug messages in console
    bool discordEnabled;        // Enable Discord bot
    
    // Twitch settings
    std::string twitchUsername;     // Bot's Twitch username
    std::string twitchOAuth;        // OAuth token (oauth:xxxxx)
    std::string twitchChannel;      // Channel to join (without #)
    bool twitchEnabled;             // Enable Twitch bot
    
    // Announcement settings
    std::string announceMessage;    // Message to announce
    std::string announceDiscordChannel; // Discord channel ID for announcements
    int announceHours;              // Hours between announcements
    int announceMins;               // Minutes between announcements
    bool announceDiscord;           // Announce on Discord
    bool announceTwitch;            // Announce on Twitch
    
    BotConfig() : profileName(""), baseUrl("https://api.kindroid.ai/v1"), personaName("User"), 
                  debugMode(false), discordEnabled(true), twitchEnabled(false),
                  announceHours(0), announceMins(30), announceDiscord(false), announceTwitch(false) {}
};

// Simple JSON parser/builder (minimal implementation)
class SimpleJSON {
public:
    static std::string escape(const std::string& str);
    static std::string unescape(const std::string& str);
    static std::string buildObject(const std::map<std::string, std::string>& obj);
    static std::map<std::string, std::string> parseObject(const std::string& json);
    static std::string getString(const std::map<std::string, std::string>& obj, const std::string& key);
    static std::vector<std::map<std::string, std::string>> parseArray(const std::string& json);
    static std::string buildConversationArray(const std::vector<std::map<std::string, std::string>>& conversation);
};

// Profile Manager - handles multiple bot profiles
class ProfileManager {
public:
    static std::vector<BotConfig> loadAllProfiles(const std::string& filename = "profiles.json");
    static bool saveAllProfiles(const std::vector<BotConfig>& profiles, const std::string& filename = "profiles.json");
    static bool addOrUpdateProfile(const BotConfig& profile, const std::string& filename = "profiles.json");
    static bool deleteProfile(const std::string& profileName, const std::string& filename = "profiles.json");
    static BotConfig* findProfile(std::vector<BotConfig>& profiles, const std::string& profileName);
    static std::string buildProfilesJson(const std::vector<BotConfig>& profiles);
    static std::vector<BotConfig> parseProfilesJson(const std::string& json);
};

// Configuration Manager (legacy - for single config compatibility)
class ConfigManager {
public:
    static bool saveConfig(const BotConfig& config, const std::string& filename = "config.json");
    static bool loadConfig(BotConfig& config, const std::string& filename = "config.json");
};

// Kindroid API Client
class KindroidAPI {
private:
    std::string apiKey;
    std::string aiId;
    std::string baseUrl;
    
public:
    KindroidAPI(const std::string& key, const std::string& id, const std::string& url);
    std::string sendMessage(const std::string& username, const std::string& channelName, const std::string& message);
    
private:
    std::string httpsRequest(const std::string& host, const std::string& path, const std::string& body);
};

// Discord WebSocket Client
class DiscordBot {
private:
    std::string token;
    std::atomic<bool> running;
    std::thread botThread;
    KindroidAPI* kindroid;
    HWND consoleHwnd;
    
    std::string sessionId;
    int sequenceNumber;
    std::atomic<bool> shouldReconnect;
    
    SOCKET currentSocket;
    std::mutex socketMutex;
    std::map<std::string, std::string> channelNames; // channelId -> channelName
    std::string guildName; // Server name
    std::string lastChannelId; // Last channel that had activity (for announcements)
	
public:
    DiscordBot(const std::string& token, KindroidAPI* api, HWND console);
    ~DiscordBot();
    
    void start();
    void stop();
    bool isRunning() const { return running; }
    void sendAnnouncement(const std::string& message, const std::string& channelId = ""); // For announcements
    
private:
    void run();
    void connectWebSocket();
    void handleGatewayMessage(const std::string& message, struct SchannelContext* ssl);
    void sendHeartbeat(struct SchannelContext* ssl, int interval);
    void sendIdentify(struct SchannelContext* ssl);
    void sendResume(struct SchannelContext* ssl);
    void handleDispatch(const std::map<std::string, std::string>& data);
    void processMessage(const std::map<std::string, std::string>& messageData);
    std::pair<std::string, std::string> getChannelInfo(const std::string& channelId);
    
    std::string httpRequest(const std::string& host, const std::string& path);
    void sendDiscordMessage(const std::string& channelId, const std::string& content);
    
    void log(const std::string& message);
};

// Twitch IRC Bot
class TwitchBot {
private:
    std::string username;
    std::string oauthToken;
    std::string channel;
    std::atomic<bool> running;
    std::thread botThread;
    KindroidAPI* kindroid;
    HWND consoleHwnd;
    
    SOCKET currentSocket;
    struct SchannelContext* currentSSL; // For sending announcements
    std::mutex socketMutex;
    
public:
    TwitchBot(const std::string& user, const std::string& oauth, const std::string& chan, 
              KindroidAPI* api, HWND console);
    ~TwitchBot();
    
    void start();
    void stop();
    bool isRunning() const { return running; }
    void sendAnnouncement(const std::string& message); // For announcements
    
private:
    void run();
    void connectIRC();
    void handleMessage(const std::string& line, struct SchannelContext* ssl);
    void sendIRCMessage(struct SchannelContext* ssl, const std::string& message);
    void sendChatMessage(struct SchannelContext* ssl, const std::string& message);
    void processChatMessage(struct SchannelContext* ssl, const std::string& user, const std::string& message);
    
    void log(const std::string& message);
};

// Utility functions
std::string wstringToString(const std::wstring& wstr);
std::wstring stringToWstring(const std::string& str);
std::string censorString(const std::string& str);
std::string getCurrentTimestamp();
std::string base64Encode(const std::string& input);

// Global variables
extern HINSTANCE g_hInstance;
extern HWND g_hwndMain;
extern BotConfig g_config;
extern DiscordBot* g_bot;
extern TwitchBot* g_twitchBot;
extern KindroidAPI* g_kindroid;
extern std::vector<BotConfig> g_profiles;
extern std::string g_currentProfileName;
extern bool g_debugMode;

// Window procedures
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CreateGUI(HWND hwnd);
void OnStartBot(HWND hwnd);
void OnStopBot(HWND hwnd);
void OnSaveConfig(HWND hwnd);
void OnLoadConfig(HWND hwnd);
void AppendConsoleText(HWND hwnd, const std::string& text);

// Profile management functions
void OnProfileSelected(HWND hwnd);
void OnSaveProfile(HWND hwnd);
void OnDeleteProfile(HWND hwnd);
void OnNewProfile(HWND hwnd);
void RefreshProfileCombo(HWND hwnd);
void LoadProfileToGUI(HWND hwnd, const BotConfig& profile);
