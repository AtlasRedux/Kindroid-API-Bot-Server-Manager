#include "KindroidBot.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

DiscordBot::DiscordBot(const std::string& token, KindroidAPI* api, HWND console)
    : token(token), kindroid(api), consoleHwnd(console), running(false),
      sequenceNumber(0), shouldReconnect(true), currentSocket(INVALID_SOCKET) {
}

DiscordBot::~DiscordBot() {
    stop();
}

void DiscordBot::start() {
    if (running) return;
    
    running = true;
    shouldReconnect = true;
    
    log("[INFO] Starting bot thread...");
    
    botThread = std::thread(&DiscordBot::run, this);
}

void DiscordBot::stop() {
    if (!running) return;
    
    log("[INFO] Stopping bot...");
    shouldReconnect = false;
    running = false;
    
    // Close the current socket to unblock any pending recv() calls
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (currentSocket != INVALID_SOCKET) {
            closesocket(currentSocket);
            currentSocket = INVALID_SOCKET;
        }
    }
    
    // Don't join here - let the thread finish on its own
    // We'll detach it instead to avoid blocking the GUI
    if (botThread.joinable()) {
        botThread.detach();
    }
    
    log("[INFO] Bot stopped");
}

void DiscordBot::log(const std::string& message) {
    std::string logMsg = message;
    if (logMsg.back() != '\n') logMsg += "\n";
    
    // Always write to log file (including DEBUG)
    FILE* logFile = fopen("log.txt", "a");
    if (logFile) {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        fprintf(logFile, "[%02d:%02d:%02d] %s", t->tm_hour, t->tm_min, t->tm_sec, logMsg.c_str());
        fclose(logFile);
    }
    
    // Filter out DEBUG messages from GUI if debug mode is off
    if (!g_debugMode && message.find("[DEBUG]") != std::string::npos) {
        return;
    }
    
    if (consoleHwnd && IsWindow(consoleHwnd)) {
        // Use SendMessage instead of PostMessage for synchronous delivery
        char* msgCopy = _strdup(logMsg.c_str());
        SendMessageA(consoleHwnd, WM_USER + 100, 0, (LPARAM)msgCopy);
        // Don't free here - the handler will free it
    }
}

void DiscordBot::run() {
    log("[INFO] Bot thread starting");
    
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    if (wsaResult != 0) {
        log("[ERROR] WSAStartup failed");
        running = false;
        return;
    }
    
    log("[INFO] Connecting to Discord...");
    
    while (running && shouldReconnect) {
        try {
            connectWebSocket();
        } catch (const std::exception& e) {
            log("[ERROR] Exception: " + std::string(e.what()));
        } catch (...) {
            log("[ERROR] Unknown exception");
        }
        
        if (shouldReconnect && running) {
            log("[INFO] Reconnecting in 5 seconds...");
            for (int i = 0; i < 50 && running; i++) {
                Sleep(100);
            }
        }
    }
    
    WSACleanup();
    log("[INFO] Bot thread stopped");
    running = false;
}

// WebSocket helper functions
static std::string makeWebSocketHandshake(const std::string& host, const std::string& path) {
    std::string key = base64Encode("kindroid-bot-key");
    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n\r\n";
    return req.str();
}

static bool sendWSFrame(SchannelContext* ssl, const std::string& data) {
    std::vector<unsigned char> frame;
    frame.push_back(0x81); // FIN + text frame
    
    size_t len = data.length();
    if (len < 126) {
        frame.push_back((unsigned char)(0x80 | len));
    } else if (len < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back((unsigned char)((len >> 8) & 0xFF));
        frame.push_back((unsigned char)(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((unsigned char)((len >> (i * 8)) & 0xFF));
        }
    }
    
    // Mask key
    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
    for (int i = 0; i < 4; i++) frame.push_back(mask[i]);
    
    // Masked payload
    for (size_t i = 0; i < data.length(); i++) {
        frame.push_back(data[i] ^ mask[i % 4]);
    }
    
    return SchannelSend(ssl, frame.data(), (int)frame.size()) == (int)frame.size();
}

static std::string recvWSFrame(SchannelContext* ssl) {
    unsigned char header[2];
    int headerResult = SchannelRecv(ssl, header, 2);
    
    if (headerResult != 2) {
        return "";
    }
    
    int opcode = header[0] & 0x0F;
    uint64_t payloadLen = header[1] & 0x7F;
    
    if (payloadLen == 126) {
        unsigned char extLen[2];
        int extResult = SchannelRecv(ssl, extLen, 2);
        if (extResult != 2) return "";
        payloadLen = (extLen[0] << 8) | extLen[1];
    } else if (payloadLen == 127) {
        unsigned char extLen[8];
        if (SchannelRecv(ssl, extLen, 8) != 8) return "";
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | extLen[i];
        }
    }
    
    std::string payload;
    payload.resize((size_t)payloadLen);
    
    size_t received = 0;
    while (received < payloadLen) {
        int r = SchannelRecv(ssl, &payload[received], (int)(payloadLen - received));
        if (r <= 0) {
            return "";
        }
        received += r;
    }
    
    if (opcode == 0x8) { // Close frame
        return ""; // Signal connection closed
    }
    if (opcode == 0x9) { // Ping
        sendWSFrame(ssl, payload);
        return recvWSFrame(ssl);
    }
    
    return payload;
}

void DiscordBot::connectWebSocket() {
    log("[INFO] Getting Discord Gateway URL...");
    
    std::string gatewayResp = httpRequest("discord.com", "/api/v10/gateway");
    if (gatewayResp.empty()) {
        log("[ERROR] Failed to get gateway URL");
        return;
    }
    
    auto gatewayData = SimpleJSON::parseObject(gatewayResp);
    std::string wsUrl = SimpleJSON::getString(gatewayData, "url");
    if (wsUrl.empty()) {
        log("[ERROR] Invalid gateway response");
        return;
    }
    
    // Parse URL
    size_t hostStart = wsUrl.find("://");
    if (hostStart == std::string::npos) {
        log("[ERROR] Invalid URL");
        return;
    }
    hostStart += 3;
    
    std::string host = wsUrl.substr(hostStart);
    size_t pathPos = host.find('/');
    if (pathPos != std::string::npos) {
        host = host.substr(0, pathPos);
    }
    
    log("[INFO] Connecting to: " + host);
    
    // Create socket
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    if (getaddrinfo(host.c_str(), "443", &hints, &result) != 0) {
        log("[ERROR] DNS resolution failed");
        return;
    }
    
    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        log("[ERROR] Socket creation failed");
        freeaddrinfo(result);
        return;
    }
    
    // Store socket so Stop button can close it
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        currentSocket = sock;
    }
    
    // Set timeouts - longer timeout since heartbeats keep connection alive
    DWORD timeout = 60000; // 60 seconds
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        log("[ERROR] Connection failed");
        closesocket(sock);
        freeaddrinfo(result);
        return;
    }
    
    freeaddrinfo(result);
    log("[INFO] TCP connected, starting TLS handshake...");
    
    // Create Schannel SSL context
    SchannelContext* ssl = SchannelCreate(sock);
    if (!SchannelHandshake(ssl, host.c_str())) {
        log("[ERROR] TLS handshake failed");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    
    log("[INFO] TLS established, performing WebSocket handshake...");
    
    // WebSocket handshake
    std::string wsHandshake = makeWebSocketHandshake(host, "/?v=10&encoding=json");
    SchannelSend(ssl, wsHandshake.c_str(), (int)wsHandshake.length());
    
    // Read HTTP response
    char httpResp[4096];
    int httpLen = SchannelRecv(ssl, httpResp, sizeof(httpResp) - 1);
    if (httpLen <= 0 || std::string(httpResp, httpLen).find("101") == std::string::npos) {
        log("[ERROR] WebSocket handshake failed");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    
    log("[INFO] WebSocket connected!");
    
    // Wait for HELLO
    log("[DEBUG] Waiting for HELLO message...");
    std::string helloMsg = recvWSFrame(ssl);
    if (helloMsg.empty()) {
        log("[ERROR] No HELLO received");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    
    log("[INFO] Received HELLO (length: " + std::to_string(helloMsg.length()) + ")");
    log("[DEBUG] HELLO content: " + helloMsg.substr(0, 200) + "...");
    
    // Parse heartbeat interval
    int heartbeatInterval = 41250;
    size_t hbPos = helloMsg.find("heartbeat_interval");
    if (hbPos != std::string::npos) {
        size_t colonPos = helloMsg.find(':', hbPos);
        if (colonPos != std::string::npos) {
            size_t numStart = colonPos + 1;
            while (numStart < helloMsg.length() && !isdigit(helloMsg[numStart])) numStart++;
            size_t numEnd = numStart;
            while (numEnd < helloMsg.length() && isdigit(helloMsg[numEnd])) numEnd++;
            if (numEnd > numStart) {
                heartbeatInterval = std::stoi(helloMsg.substr(numStart, numEnd - numStart));
            }
        }
    }
    
    log("[INFO] Heartbeat interval: " + std::to_string(heartbeatInterval) + "ms");
    
    // Send IDENTIFY
    sendIdentify(ssl);
    
    // Start heartbeat thread
    std::atomic<bool> hbRunning(true);
    std::thread hbThread([this, ssl, heartbeatInterval, &hbRunning]() {
        log("[DEBUG] Heartbeat thread started");
        while (hbRunning && running) {
            // Send heartbeat first
            log("[DEBUG] Sending heartbeat...");
            sendHeartbeat(ssl, heartbeatInterval);
            
            // Then wait for next interval
            Sleep(heartbeatInterval);
        }
        log("[DEBUG] Heartbeat thread stopped");
    });
    
    log("[INFO] Entering main message loop...");
    
    // Main message loop
    int messageCount = 0;
    while (running) {
        log("[DEBUG] Waiting for next message... (count: " + std::to_string(messageCount) + ")");
        std::string msg = recvWSFrame(ssl);
        
        if (msg.empty()) {
            log("[ERROR] Connection lost (empty message received)");
            log("[ERROR] This usually means Discord closed the connection");
            log("[ERROR] Check: 1) Token is valid, 2) MESSAGE_CONTENT intent is enabled in Discord Dev Portal");
            break;
        }
        
        messageCount++;
        log("[DEBUG] Message #" + std::to_string(messageCount) + " received, length: " + std::to_string(msg.length()));
        handleGatewayMessage(msg, ssl);
    }
    
    log("[INFO] Exiting main message loop");
    hbRunning = false;
    if (hbThread.joinable()) hbThread.join();
    
    SchannelDestroy(ssl);
    closesocket(sock);
    
    // Clear socket handle
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        currentSocket = INVALID_SOCKET;
    }
}

void DiscordBot::sendHeartbeat(SchannelContext* ssl, int interval) {
    std::string hb = "{\"op\":1,\"d\":";
    hb += (sequenceNumber > 0) ? std::to_string(sequenceNumber) : "null";
    hb += "}";
    sendWSFrame(ssl, hb);
}

void DiscordBot::sendIdentify(SchannelContext* ssl) {
    log("[INFO] Sending IDENTIFY...");
    
    // Intents: GUILDS (1) + GUILD_MESSAGES (512) + MESSAGE_CONTENT (32768) = 33281
    std::string identify = "{\"op\":2,\"d\":{";
    identify += "\"token\":\"" + token + "\",";
    identify += "\"intents\":33281,";
    identify += "\"properties\":{";
    identify += "\"os\":\"windows\",";
    identify += "\"browser\":\"kindroid_bot\",";
    identify += "\"device\":\"kindroid_bot\"";
    identify += "}}}";
    
    log("[DEBUG] IDENTIFY payload: " + identify);
    
    bool sent = sendWSFrame(ssl, identify);
    log("[DEBUG] IDENTIFY send result: " + std::to_string(sent));
}

void DiscordBot::sendResume(SchannelContext* ssl) {
    log("[INFO] Sending RESUME...");
    
    std::string resume = "{\"op\":6,\"d\":{";
    resume += "\"token\":\"" + token + "\",";
    resume += "\"session_id\":\"" + sessionId + "\",";
    resume += "\"seq\":" + std::to_string(sequenceNumber);
    resume += "}}";
    
    sendWSFrame(ssl, resume);
}

void DiscordBot::handleGatewayMessage(const std::string& message, SchannelContext* ssl) {
    log("[DEBUG] Received gateway message (length: " + std::to_string(message.length()) + ")");
    
    // Skip empty messages
    if (message.empty()) {
        log("[DEBUG] Empty message, skipping");
        return;
    }
    
    // Extract opcode directly from JSON string (more reliable than our simple parser)
    int op = -1;
    size_t opPos = message.find("\"op\":");
    if (opPos != std::string::npos) {
        size_t numStart = opPos + 5;
        while (numStart < message.length() && !isdigit(message[numStart]) && message[numStart] != '-') numStart++;
        if (numStart < message.length()) {
            size_t numEnd = numStart;
            while (numEnd < message.length() && (isdigit(message[numEnd]) || message[numEnd] == '-')) numEnd++;
            if (numEnd > numStart) {
                try {
                    op = std::stoi(message.substr(numStart, numEnd - numStart));
                } catch (...) {
                    log("[DEBUG] Failed to parse opcode");
                }
            }
        }
    }
    
    if (op == -1) {
        // Log first part of message to help debug
        int previewLen = (message.length() < 100) ? (int)message.length() : 100;
        log("[DEBUG] No opcode found in message: " + message.substr(0, previewLen));
        return;
    }
    
    log("[DEBUG] Gateway opcode: " + std::to_string(op));
    
    // Extract sequence number
    size_t seqPos = message.find("\"s\":");
    if (seqPos != std::string::npos) {
        size_t numStart = seqPos + 4;
        while (numStart < message.length() && !isdigit(message[numStart]) && message[numStart] != 'n') numStart++;
        if (numStart < message.length() && message[numStart] != 'n') { // not "null"
            size_t numEnd = numStart;
            while (numEnd < message.length() && isdigit(message[numEnd])) numEnd++;
            if (numEnd > numStart) {
                sequenceNumber = std::stoi(message.substr(numStart, numEnd - numStart));
            }
        }
    }
    
    switch (op) {
        case 0: { // Dispatch - need braces for variable declarations
            log("[INFO] Dispatch event received");
            
            // Extract event type
            std::string eventType;
            size_t tPos = message.find("\"t\":");
            if (tPos != std::string::npos) {
                size_t strStart = message.find('"', tPos + 4) + 1;
                size_t strEnd = message.find('"', strStart);
                eventType = message.substr(strStart, strEnd - strStart);
            }
            
            log("[INFO] Event type: " + eventType);
            
            if (eventType == "READY") {
                log("[INFO] Bot is READY!");
                // Extract session_id
                size_t sessPos = message.find("\"session_id\":");
                if (sessPos != std::string::npos) {
                    size_t start = message.find('"', sessPos + 13) + 1;
                    size_t end = message.find('"', start);
                    sessionId = message.substr(start, end - start);
                    log("[INFO] Session ID: " + sessionId);
                }
            } else if (eventType == "MESSAGE_CREATE") {
                log("[INFO] MESSAGE_CREATE event received");
                
                // Extract content
                std::string content;
                size_t contentPos = message.find("\"content\":\"");
                if (contentPos != std::string::npos) {
                    size_t start = contentPos + 11;
                    size_t end = message.find('"', start);
                    // Handle escaped quotes in content
                    while (end != std::string::npos && end > 0 && message[end - 1] == '\\') {
                        end = message.find('"', end + 1);
                    }
                    std::string rawContent = message.substr(start, end - start);
                    // Unescape the JSON string
                    content = SimpleJSON::unescape(rawContent);
                }
                
                // Extract author username
                std::string username = "unknown";
                size_t userPos = message.find("\"author\":");
                if (userPos != std::string::npos) {
                    size_t unamePos = message.find("\"username\":\"", userPos);
                    if (unamePos != std::string::npos) {
                        size_t start = unamePos + 12;
                        size_t end = message.find('"', start);
                        username = message.substr(start, end - start);
                    }
                }
                
                // Extract channel ID
                std::string channelId;
                size_t chanPos = message.find("\"channel_id\":\"");
                if (chanPos != std::string::npos) {
                    size_t start = chanPos + 14;
                    size_t end = message.find('"', start);
                    channelId = message.substr(start, end - start);
                }
                
                log("[DEBUG] Content length: " + std::to_string(content.length()) + " bytes");
                log("[DEBUG] Username: " + username);
                log("[DEBUG] Channel: " + channelId);
                
                // Check for bot mention
                if (content.find("<@") != std::string::npos) {
                    // Remove mention
                    size_t mentionEnd = content.find('>', content.find("<@"));
                    if (mentionEnd != std::string::npos) {
                        content = content.substr(mentionEnd + 1);
                    }
                    
                    // Trim
                    size_t start = content.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        content = content.substr(start);
                    }
                    
                    if (!content.empty()) {
                        log("[DISCORD] " + username + ": " + content);
                        
                        // Fetch actual channel and server names
                        auto [channelName, serverName] = getChannelInfo(channelId);
                        std::string contextName = serverName + " / #" + channelName;
                        
                        std::string response = kindroid->sendMessage(username, contextName, content);
                        
                        log("[KINDROID] " + response);
                        
                        if (!response.empty() && response.find("[ERROR]") == std::string::npos) {
                            sendDiscordMessage(channelId, response);
                        }
                    }
                }
            }
            break;
        }
            
        case 1: // Heartbeat
            log("[DEBUG] Heartbeat requested by server");
            sendHeartbeat(ssl, 41250);
            break;
            
        case 7: // Reconnect
            log("[INFO] Discord requested reconnect");
            shouldReconnect = true;
            break;
            
        case 9: // Invalid session
            log("[WARNING] Invalid session");
            sessionId.clear();
            Sleep(1000);
            break;
            
        case 10: // Hello
            log("[DEBUG] Hello opcode received");
            break;
            
        case 11: // Heartbeat ACK
            log("[DEBUG] Heartbeat ACK received");
            break;
            
        default:
            log("[WARNING] Unknown opcode: " + std::to_string(op));
    }
}

void DiscordBot::handleDispatch(const std::map<std::string, std::string>& data) {
    std::string eventType = SimpleJSON::getString(data, "t");
    
    log("[INFO] Dispatch event type: " + eventType);
    
    if (eventType == "READY") {
        log("[INFO] Bot is READY!");
        std::string dField = SimpleJSON::getString(data, "d");
        size_t sessPos = dField.find("session_id");
        if (sessPos != std::string::npos) {
            size_t start = dField.find('"', sessPos + 12) + 1;
            size_t end = dField.find('"', start);
            sessionId = dField.substr(start, end - start);
            log("[INFO] Session ID: " + sessionId);
        }
    } else if (eventType == "MESSAGE_CREATE") {
        log("[INFO] MESSAGE_CREATE event received");
        std::string dField = SimpleJSON::getString(data, "d");
        auto messageData = SimpleJSON::parseObject(dField);
        processMessage(messageData);
    } else {
        log("[DEBUG] Unhandled event: " + eventType);
    }
}

void DiscordBot::processMessage(const std::map<std::string, std::string>& messageData) {
    std::string content = SimpleJSON::getString(messageData, "content");
    std::string authorField = SimpleJSON::getString(messageData, "author");
    std::string channelId = SimpleJSON::getString(messageData, "channel_id");
    
    // Track last active channel for announcements
    lastChannelId = channelId;
    
    // Extract username
    std::string username = "unknown";
    size_t unPos = authorField.find("\"username\"");
    if (unPos != std::string::npos) {
        size_t start = authorField.find('"', unPos + 11) + 1;
        size_t end = authorField.find('"', start);
        username = authorField.substr(start, end - start);
    }
    
    log("[DEBUG] Message from " + username + " in channel " + channelId);
    
    // Check for bot mention
    if (content.find("<@") == std::string::npos) {
        log("[DEBUG] Message doesn't mention bot, ignoring");
        return;
    }
    
    log("[DEBUG] Bot was mentioned!");
    
    // Remove mention
    size_t mentionEnd = content.find('>', content.find("<@"));
    if (mentionEnd != std::string::npos) {
        content = content.substr(mentionEnd + 1);
    }
    
    // Trim
    size_t start = content.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        content = content.substr(start);
    }
    
    if (content.empty()) {
        log("[DEBUG] Message empty after removing mention");
        return;
    }
    
    log("[DISCORD] " + username + ": " + content);
    
    // Fetch actual channel and server names
    auto [channelName, serverName] = getChannelInfo(channelId);
    std::string contextName = serverName + " / #" + channelName;
    log("[DEBUG] Context: " + contextName);
    
    log("[DEBUG] Sending to Kindroid API...");
    std::string response = kindroid->sendMessage(username, contextName, content);
    
    log("[KINDROID] " + response);
    
    if (!response.empty() && response.find("[ERROR]") == std::string::npos) {
        log("[DEBUG] Sending response to Discord...");
        sendDiscordMessage(channelId, response);
        log("[DEBUG] Response sent successfully");
    }
}

std::pair<std::string, std::string> DiscordBot::getChannelInfo(const std::string& channelId) {
    // Check cache first
    std::string channelName = "unknown-channel";
    std::string serverName = guildName.empty() ? "unknown-server" : guildName;
    
    auto it = channelNames.find(channelId);
    if (it != channelNames.end()) {
        channelName = it->second;
        return {channelName, serverName};
    }
    
    // Fetch from Discord API
    std::string path = "/api/v10/channels/" + channelId;
    std::string response = httpRequest("discord.com", path);
    
    if (!response.empty()) {
        // Parse channel name
        size_t namePos = response.find("\"name\":\"");
        if (namePos != std::string::npos) {
            size_t start = namePos + 8;
            size_t end = response.find('"', start);
            if (end != std::string::npos) {
                channelName = response.substr(start, end - start);
                // Cache it
                channelNames[channelId] = channelName;
            }
        }
        
        // Parse guild_id to fetch server name if we don't have it
        if (guildName.empty()) {
            size_t guildPos = response.find("\"guild_id\":\"");
            if (guildPos != std::string::npos) {
                size_t start = guildPos + 12;
                size_t end = response.find('"', start);
                if (end != std::string::npos) {
                    std::string guildId = response.substr(start, end - start);
                    
                    // Fetch guild info
                    std::string guildPath = "/api/v10/guilds/" + guildId;
                    std::string guildResponse = httpRequest("discord.com", guildPath);
                    
                    if (!guildResponse.empty()) {
                        size_t gNamePos = guildResponse.find("\"name\":\"");
                        if (gNamePos != std::string::npos) {
                            size_t gStart = gNamePos + 8;
                            size_t gEnd = guildResponse.find('"', gStart);
                            if (gEnd != std::string::npos) {
                                guildName = guildResponse.substr(gStart, gEnd - gStart);
                                serverName = guildName;
                            }
                        }
                    }
                }
            }
        } else {
            serverName = guildName;
        }
    }
    
    return {channelName, serverName};
}

void DiscordBot::sendDiscordMessage(const std::string& channelId, const std::string& content) {
    log("[DEBUG] sendDiscordMessage called for channel: " + channelId);
    log("[DEBUG] Response length: " + std::to_string(content.length()) + " bytes");
    
    std::map<std::string, std::string> payload;
    payload["content"] = content;
    std::string jsonPayload = SimpleJSON::buildObject(payload);
    
    std::string path = "/api/v10/channels/" + channelId + "/messages";
    
    HINTERNET hSession = WinHttpOpen(L"KindroidBot/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log("[ERROR] WinHttpOpen failed");
        return;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        log("[ERROR] WinHttpConnect failed");
        WinHttpCloseHandle(hSession);
        return;
    }
    
    std::wstring wpath = stringToWstring(path);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        log("[ERROR] WinHttpOpenRequest failed");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }
    
    std::string authHeader = "Authorization: Bot " + token;
    std::string contentType = "Content-Type: application/json; charset=utf-8";
    std::wstring headers = stringToWstring(authHeader + "\r\n" + contentType);
    
    BOOL sendResult = WinHttpSendRequest(hRequest, headers.c_str(), -1,
        (LPVOID)jsonPayload.c_str(), (DWORD)jsonPayload.length(),
        (DWORD)jsonPayload.length(), 0);
    
    if (!sendResult) {
        log("[ERROR] WinHttpSendRequest failed");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }
    
    BOOL recvResult = WinHttpReceiveResponse(hRequest, NULL);
    if (!recvResult) {
        log("[ERROR] WinHttpReceiveResponse failed");
    } else {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, 
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &statusCode, &statusCodeSize, NULL);
        log("[DEBUG] Discord API response code: " + std::to_string(statusCode));
        
        if (statusCode != 200 && statusCode != 201) {
            log("[ERROR] Discord API error: " + std::to_string(statusCode));
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    log("[DEBUG] sendDiscordMessage completed");
}

std::string DiscordBot::httpRequest(const std::string& host, const std::string& path) {
    HINTERNET hSession = WinHttpOpen(L"KindroidBot/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    
    std::wstring whost = stringToWstring(host);
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    std::wstring wpath = stringToWstring(path);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    std::string authHeader = "Authorization: Bot " + token;
    std::wstring headers = stringToWstring(authHeader) + L"\r\n";
    
    WinHttpSendRequest(hRequest, headers.c_str(), -1,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string response;
    DWORD dwSize, dwDownloaded;
    do {
        dwSize = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize == 0) break;
        
        char* buf = new char[dwSize + 1];
        ZeroMemory(buf, dwSize + 1);
        
        if (WinHttpReadData(hRequest, buf, dwSize, &dwDownloaded)) {
            response.append(buf, dwDownloaded);
        }
        delete[] buf;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}

void DiscordBot::sendAnnouncement(const std::string& message, const std::string& channelId) {
    // Use provided channel ID, or fall back to last active channel
    std::string targetChannel = channelId.empty() ? lastChannelId : channelId;
    
    if (targetChannel.empty()) {
        log("[ANNOUNCE] No channel available for announcement (configure Channel ID or wait for activity)");
        return;
    }
    
    if (!running) {
        log("[ANNOUNCE] Bot not running, cannot send announcement");
        return;
    }
    
    log("[ANNOUNCE] Sending to Discord channel " + targetChannel + ": " + message);
    sendDiscordMessage(targetChannel, message);
}
