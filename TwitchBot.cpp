#include "KindroidBot.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

TwitchBot::TwitchBot(const std::string& user, const std::string& oauth, const std::string& chan,
                     KindroidAPI* api, HWND console)
    : username(user), oauthToken(oauth), channel(chan), kindroid(api), consoleHwnd(console),
      running(false), currentSocket(INVALID_SOCKET), currentSSL(nullptr) {
    
    // Ensure channel is lowercase and without #
    std::transform(channel.begin(), channel.end(), channel.begin(), ::tolower);
    if (!channel.empty() && channel[0] == '#') {
        channel = channel.substr(1);
    }
    
    // Ensure username is lowercase
    std::transform(username.begin(), username.end(), username.begin(), ::tolower);
    
    // Ensure OAuth token has oauth: prefix
    if (!oauthToken.empty() && oauthToken.substr(0, 6) != "oauth:") {
        oauthToken = "oauth:" + oauthToken;
    }
}

TwitchBot::~TwitchBot() {
    stop();
}

void TwitchBot::start() {
    if (running) return;
    
    running = true;
    log("[INFO] Starting Twitch bot...");
    log("[INFO] (Get OAuth token from https://twitchtokengenerator.com/)");
    botThread = std::thread(&TwitchBot::run, this);
}

void TwitchBot::stop() {
    if (!running) return;
    
    log("[INFO] Stopping Twitch bot...");
    running = false;
    
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (currentSocket != INVALID_SOCKET) {
            closesocket(currentSocket);
            currentSocket = INVALID_SOCKET;
        }
    }
    
    if (botThread.joinable()) {
        botThread.detach();
    }
    
    log("[INFO] Twitch bot stopped");
}

void TwitchBot::log(const std::string& message) {
    std::string logMsg = "[TWITCH] " + message;
    if (logMsg.back() != '\n') logMsg += "\n";
    
    // Always write to log file (including DEBUG)
    FILE* logFile = fopen("log.txt", "a");
    if (logFile) {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        fprintf(logFile, "[%02d:%02d:%02d] %s", t->tm_hour, t->tm_min, t->tm_sec, logMsg.c_str());
        fclose(logFile);
    }
    
    // Filter DEBUG messages from GUI based on global flag
    if (!g_debugMode && message.find("[DEBUG]") != std::string::npos) {
        return;
    }
    
    if (consoleHwnd && IsWindow(consoleHwnd)) {
        char* msgCopy = _strdup(logMsg.c_str());
        SendMessageA(consoleHwnd, WM_USER + 100, 0, (LPARAM)msgCopy);
    }
}

void TwitchBot::run() {
    log("[INFO] Twitch bot thread starting");
    
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    if (wsaResult != 0) {
        log("[ERROR] WSAStartup failed");
        running = false;
        return;
    }
    
    while (running) {
        try {
            connectIRC();
        } catch (const std::exception& e) {
            log("[ERROR] Exception: " + std::string(e.what()));
        } catch (...) {
            log("[ERROR] Unknown exception");
        }
        
        if (running) {
            log("[INFO] Reconnecting in 5 seconds...");
            for (int i = 0; i < 50 && running; i++) {
                Sleep(100);
            }
        }
    }
    
    WSACleanup();
    log("[INFO] Twitch bot thread stopped");
    running = false;
}

void TwitchBot::connectIRC() {
    log("[INFO] Connecting to Twitch IRC...");
    
    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        log("[ERROR] Failed to create socket");
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        currentSocket = sock;
    }
    
    // Resolve host
    struct addrinfo hints = {0}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    // Twitch IRC WebSocket server
    if (getaddrinfo("irc-ws.chat.twitch.tv", "443", &hints, &result) != 0) {
        log("[ERROR] Failed to resolve Twitch IRC host");
        closesocket(sock);
        return;
    }
    
    // Connect
    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        log("[ERROR] Failed to connect to Twitch");
        freeaddrinfo(result);
        closesocket(sock);
        return;
    }
    freeaddrinfo(result);
    
    log("[DEBUG] TCP connected, starting TLS handshake...");
    
    // Create SSL context
    SchannelContext* ssl = SchannelCreate(sock);
    if (!ssl) {
        log("[ERROR] Failed to create SSL context");
        closesocket(sock);
        return;
    }
    
    // TLS handshake
    if (!SchannelHandshake(ssl, "irc-ws.chat.twitch.tv")) {
        log("[ERROR] TLS handshake failed");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    
    log("[DEBUG] TLS established, sending WebSocket upgrade...");
    
    // WebSocket handshake - Twitch requires specific headers
    std::string wsKey = base64Encode("twitch-kindroid-bot!");
    std::string wsRequest = 
        "GET / HTTP/1.1\r\n"
        "Host: irc-ws.chat.twitch.tv\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + wsKey + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: https://irc-ws.chat.twitch.tv\r\n\r\n";
    
    log("[DEBUG] Sending WebSocket request...");
    
    if (SchannelSend(ssl, wsRequest.c_str(), (int)wsRequest.length()) <= 0) {
        log("[ERROR] Failed to send WebSocket upgrade");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    
    // Read WebSocket upgrade response
    char buffer[4096];
    int received = SchannelRecv(ssl, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
        log("[ERROR] Failed to receive WebSocket upgrade response");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    buffer[received] = '\0';
    
    log("[DEBUG] Got response: " + std::string(buffer, (received < 100) ? received : 100));
    
    if (strstr(buffer, "101") == nullptr) {
        log("[ERROR] WebSocket upgrade failed - expected 101 Switching Protocols");
        SchannelDestroy(ssl);
        closesocket(sock);
        return;
    }
    
    log("[INFO] WebSocket connected to Twitch IRC!");
    
    // Set socket timeout for recv (5 seconds)
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Store SSL context for announcements
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        currentSSL = ssl;
    }
    
    // Send IRC authentication
    log("[DEBUG] Sending IRC authentication...");
    
    // CAP REQ for tags (to get user info)
    sendIRCMessage(ssl, "CAP REQ :twitch.tv/tags twitch.tv/commands");
    
    // PASS (OAuth token)
    std::string passCmd = "PASS " + oauthToken;
    sendIRCMessage(ssl, passCmd);
    
    // NICK (username)
    std::string nickCmd = "NICK " + username;
    sendIRCMessage(ssl, nickCmd);
    
    log("[DEBUG] Joining channel #" + channel + "...");
    
    // JOIN channel
    std::string joinCmd = "JOIN #" + channel;
    sendIRCMessage(ssl, joinCmd);
    
    log("[INFO] Joined #" + channel);
    log("[INFO] Listening for messages mentioning @" + username + "...");
    
    // Main message loop
    std::string lineBuffer;
    
    while (running) {
        // Read WebSocket frame header
        unsigned char header[2];
        int headerLen = SchannelRecv(ssl, header, 2);
        if (headerLen <= 0) {
            // Check if it's a timeout (would return -1 or 0)
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || headerLen == 0) {
                // Timeout - that's OK, just continue
                continue;
            }
            log("[ERROR] Connection lost (header read failed, err=" + std::to_string(err) + ")");
            break;
        }
        if (headerLen != 2) {
            log("[DEBUG] Partial header read: " + std::to_string(headerLen));
            continue;
        }
        
        // Parse frame
        // int fin = (header[0] >> 7) & 1;
        int opcode = header[0] & 0x0F;
        int masked = (header[1] >> 7) & 1;
        uint64_t payloadLen = header[1] & 0x7F;
        
        // Extended payload length
        if (payloadLen == 126) {
            unsigned char ext[2];
            if (SchannelRecv(ssl, ext, 2) != 2) {
                log("[ERROR] Connection lost (ext len read failed)");
                break;
            }
            payloadLen = (ext[0] << 8) | ext[1];
        } else if (payloadLen == 127) {
            unsigned char ext[8];
            if (SchannelRecv(ssl, ext, 8) != 8) {
                log("[ERROR] Connection lost (ext len read failed)");
                break;
            }
            payloadLen = 0;
            for (int i = 0; i < 8; i++) {
                payloadLen = (payloadLen << 8) | ext[i];
            }
        }
        
        // Read mask key if present (server frames shouldn't be masked, but handle it)
        unsigned char maskKey[4] = {0};
        if (masked) {
            if (SchannelRecv(ssl, maskKey, 4) != 4) {
                log("[ERROR] Connection lost (mask read failed)");
                break;
            }
        }
        
        // Read payload
        std::string payload;
        if (payloadLen > 0) {
            payload.resize(payloadLen);
            size_t totalRead = 0;
            while (totalRead < payloadLen) {
                int toRead = (payloadLen - totalRead > 4096) ? 4096 : (int)(payloadLen - totalRead);
                int readLen = SchannelRecv(ssl, &payload[totalRead], toRead);
                if (readLen <= 0) {
                    log("[ERROR] Connection lost (payload read failed)");
                    break;
                }
                totalRead += readLen;
            }
            if (totalRead < payloadLen) break;
            
            // Unmask if needed
            if (masked) {
                for (size_t i = 0; i < payload.length(); i++) {
                    payload[i] ^= maskKey[i % 4];
                }
            }
        }
        
        // Handle frame based on opcode
        if (opcode == 0x8) { // Close frame
            log("[DEBUG] Received close frame");
            break;
        } else if (opcode == 0x9) { // Ping frame
            log("[DEBUG] Received WebSocket ping, sending pong");
            // Send pong with same payload
            std::vector<unsigned char> pong;
            pong.push_back(0x8A); // FIN + pong
            pong.push_back(0x80 | (unsigned char)payload.length()); // Masked + length
            unsigned char pongMask[4] = {0x12, 0x34, 0x56, 0x78};
            for (int i = 0; i < 4; i++) pong.push_back(pongMask[i]);
            for (size_t i = 0; i < payload.length(); i++) {
                pong.push_back(payload[i] ^ pongMask[i % 4]);
            }
            SchannelSend(ssl, pong.data(), (int)pong.size());
            continue;
        } else if (opcode == 0xA) { // Pong frame
            log("[DEBUG] Received pong");
            continue;
        } else if (opcode == 0x1 || opcode == 0x0) { // Text or continuation
            // Add to line buffer and process
            lineBuffer += payload;
            
            // Process complete lines
            size_t pos;
            while ((pos = lineBuffer.find("\r\n")) != std::string::npos) {
                std::string line = lineBuffer.substr(0, pos);
                lineBuffer = lineBuffer.substr(pos + 2);
                
                if (!line.empty()) {
                    handleMessage(line, ssl);
                }
            }
        }
    }
    
    // Cleanup
    sendIRCMessage(ssl, "QUIT");
    SchannelDestroy(ssl);
    closesocket(sock);
    
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        currentSocket = INVALID_SOCKET;
        currentSSL = nullptr;
    }
    
    log("[INFO] Disconnected from Twitch IRC");
}

void TwitchBot::sendIRCMessage(SchannelContext* ssl, const std::string& message) {
    // Send as WebSocket text frame
    std::string data = message + "\r\n";
    
    std::vector<unsigned char> frame;
    frame.push_back(0x81); // FIN + text frame
    
    size_t len = data.length();
    if (len < 126) {
        frame.push_back((unsigned char)(0x80 | len)); // Masked
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
    
    SchannelSend(ssl, frame.data(), (int)frame.size());
}

void TwitchBot::sendChatMessage(SchannelContext* ssl, const std::string& message) {
    // Split long messages (Twitch limit is 500 chars)
    const size_t MAX_LEN = 450; // Leave some room
    
    std::string remaining = message;
    while (!remaining.empty()) {
        std::string chunk;
        if (remaining.length() <= MAX_LEN) {
            chunk = remaining;
            remaining.clear();
        } else {
            // Find a good break point
            size_t breakPos = remaining.rfind(' ', MAX_LEN);
            if (breakPos == std::string::npos || breakPos < MAX_LEN / 2) {
                breakPos = MAX_LEN;
            }
            chunk = remaining.substr(0, breakPos);
            remaining = remaining.substr(breakPos);
            // Trim leading space from remaining
            while (!remaining.empty() && remaining[0] == ' ') {
                remaining = remaining.substr(1);
            }
        }
        
        std::string privmsg = "PRIVMSG #" + channel + " :" + chunk;
        sendIRCMessage(ssl, privmsg);
        
        // Small delay between chunks to avoid rate limiting
        if (!remaining.empty()) {
            Sleep(100);
        }
    }
}

void TwitchBot::handleMessage(const std::string& line, SchannelContext* ssl) {
    log("[DEBUG] IRC: " + line);
    
    // Handle PING
    if (line.substr(0, 4) == "PING") {
        std::string pong = "PONG" + line.substr(4);
        sendIRCMessage(ssl, pong);
        log("[DEBUG] Sent PONG response");
        return;
    }
    
    // Parse PRIVMSG
    // Format: @tags :user!user@user.tmi.twitch.tv PRIVMSG #channel :message
    // Or without tags: :user!user@user.tmi.twitch.tv PRIVMSG #channel :message
    
    size_t privmsgPos = line.find("PRIVMSG");
    if (privmsgPos == std::string::npos) {
        return;
    }
    
    // Extract username
    std::string sender;
    size_t userStart = line.find(':');
    if (userStart != std::string::npos) {
        // Skip tags if present
        if (line[0] == '@') {
            userStart = line.find(':', line.find(' '));
        }
        if (userStart != std::string::npos) {
            size_t userEnd = line.find('!', userStart);
            if (userEnd != std::string::npos) {
                sender = line.substr(userStart + 1, userEnd - userStart - 1);
            }
        }
    }
    
    // Extract message content
    size_t msgStart = line.find(':', privmsgPos);
    if (msgStart == std::string::npos) {
        return;
    }
    std::string content = line.substr(msgStart + 1);
    
    // Ignore messages from self
    std::string senderLower = sender;
    std::transform(senderLower.begin(), senderLower.end(), senderLower.begin(), ::tolower);
    if (senderLower == username) {
        return;
    }
    
    // Check for mention (@username)
    std::string mention = "@" + username;
    std::string contentLower = content;
    std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), ::tolower);
    
    if (contentLower.find(mention) == std::string::npos) {
        // Not mentioned, ignore
        return;
    }
    
    log("[DEBUG] Bot was mentioned by " + sender);
    
    // Remove the mention from content
    size_t mentionPos = contentLower.find(mention);
    if (mentionPos != std::string::npos) {
        content = content.substr(0, mentionPos) + content.substr(mentionPos + mention.length());
    }
    
    // Trim whitespace
    size_t start = content.find_first_not_of(" \t\n\r");
    size_t end = content.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        content = content.substr(start, end - start + 1);
    } else {
        content.clear();
    }
    
    if (content.empty()) {
        return;
    }
    
    processChatMessage(ssl, sender, content);
}

void TwitchBot::processChatMessage(SchannelContext* ssl, const std::string& user, const std::string& message) {
    log("[CHAT] " + user + ": " + message);
    
    // Process in a detached thread to avoid blocking the receive loop
    std::thread([this, ssl, user, message]() {
        // Create context for Kindroid
        std::string context = "Twitch / #" + channel;
        
        log("[DEBUG] Sending to Kindroid API...");
        std::string response = kindroid->sendMessage(user, context, message);
        
        log("[KINDROID] " + response);
        
        if (!response.empty() && response.find("[ERROR]") == std::string::npos) {
            log("[DEBUG] Sending response to Twitch chat...");
            // Use mutex to protect socket access
            std::lock_guard<std::mutex> lock(socketMutex);
            if (running && currentSocket != INVALID_SOCKET) {
                sendChatMessage(ssl, response);
                log("[DEBUG] Response sent");
            }
        }
    }).detach();
}

void TwitchBot::sendAnnouncement(const std::string& message) {
    std::lock_guard<std::mutex> lock(socketMutex);
    
    if (!running || currentSocket == INVALID_SOCKET || currentSSL == nullptr) {
        log("[ANNOUNCE] Twitch not connected, cannot send announcement");
        return;
    }
    
    log("[ANNOUNCE] Sending to Twitch #" + channel + ": " + message);
    sendChatMessage(currentSSL, message);
}
