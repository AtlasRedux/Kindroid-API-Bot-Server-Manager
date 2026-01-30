#include "KindroidBot.h"

KindroidAPI::KindroidAPI(const std::string& key, const std::string& id, const std::string& url)
    : apiKey(key), aiId(id), baseUrl(url) {
}

std::string KindroidAPI::sendMessage(const std::string& username, const std::string& channelName, const std::string& message) {
    // Build the message with context
    std::string fullMessage = "<Message to you from " + username + " in channel " + channelName + "> " + message;
    
    // Build JSON body
    std::map<std::string, std::string> body;
    body["ai_id"] = aiId;
    body["message"] = fullMessage;
    
    std::string jsonBody = SimpleJSON::buildObject(body);
    
    // Parse baseUrl to get host and path
    std::string host, path;
    size_t protocolEnd = baseUrl.find("://");
    if (protocolEnd != std::string::npos) {
        size_t hostStart = protocolEnd + 3;
        size_t pathStart = baseUrl.find('/', hostStart);
        if (pathStart != std::string::npos) {
            host = baseUrl.substr(hostStart, pathStart - hostStart);
            path = baseUrl.substr(pathStart) + "/send-message";
        } else {
            host = baseUrl.substr(hostStart);
            path = "/v1/send-message";
        }
    } else {
        return "[ERROR] Invalid base URL format";
    }
    
    return httpsRequest(host, path, jsonBody);
}

std::string KindroidAPI::httpsRequest(const std::string& host, const std::string& path, const std::string& body) {
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    std::string response;
    
    // Initialize WinHTTP
    hSession = WinHttpOpen(L"KindroidBot/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!hSession) return "[ERROR] WinHTTP session failed";
    
    // Connect to server
    std::wstring whost = stringToWstring(host);
    hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "[ERROR] Connection failed";
    }
    
    // Create request
    std::wstring wpath = stringToWstring(path);
    hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "[ERROR] Request creation failed";
    }
    
    // Set headers
    std::string authHeader = "Authorization: Bearer " + apiKey;
    std::wstring wAuthHeader = stringToWstring(authHeader);
    std::wstring headers = wAuthHeader + L"\r\nContent-Type: application/json\r\n";
    
    // Send request
    BOOL bResults = WinHttpSendRequest(hRequest,
        headers.c_str(), -1,
        (LPVOID)body.c_str(), (DWORD)body.length(),
        (DWORD)body.length(), 0);
    
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            
            if (dwSize == 0) break;
            
            char* pszOutBuffer = new char[dwSize + 1];
            ZeroMemory(pszOutBuffer, dwSize + 1);
            
            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                response.append(pszOutBuffer, dwDownloaded);
            }
            
            delete[] pszOutBuffer;
        } while (dwSize > 0);
    }
    
    // Cleanup
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    if (response.empty()) return "[ERROR] No response from API";
    
    // Check if response is JSON or plain text
    // If it starts with '{', it's JSON
    if (response[0] == '{') {
        // Parse JSON response
        std::map<std::string, std::string> responseObj = SimpleJSON::parseObject(response);
        
        // Kindroid API returns "response_text" field
        std::string aiResponse = SimpleJSON::getString(responseObj, "response_text");
        
        if (aiResponse.empty()) {
            // Try old field name for backwards compatibility
            aiResponse = SimpleJSON::getString(responseObj, "response");
        }
        
        if (aiResponse.empty()) {
            // Try to get error message
            std::string error = SimpleJSON::getString(responseObj, "error");
            if (!error.empty()) {
                return "[ERROR] API Error: " + error;
            }
            return "[ERROR] Unknown JSON format";
        }
        
        return aiResponse;
    } else {
        // Plain text response - return as-is
        return response;
    }
}
