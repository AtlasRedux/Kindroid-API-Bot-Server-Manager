#include "KindroidBot.h"

std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);
    return str;
}

std::wstring stringToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

std::string censorString(const std::string& str) {
    if (str.length() <= 8) {
        return std::string(str.length(), '*');
    }
    return str.substr(0, 4) + std::string(str.length() - 8, '*') + str.substr(str.length() - 4);
}

std::string getCurrentTimestamp() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    return std::string(buffer);
}

static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64Encode(const std::string& input) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t in_len = input.size();
    const unsigned char* bytes_to_encode = (const unsigned char*)input.c_str();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while(i++ < 3)
            ret += '=';
    }

    return ret;
}

// Simple JSON implementation
std::string SimpleJSON::escape(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                // Pass through all bytes >= 0x80 (UTF-8 continuation bytes and multibyte chars)
                // Only escape control characters < 32
                if (c < 32) {
                    char buf[8];
                    sprintf(buf, "\\u%04x", c);
                    result += buf;
                } else {
                    result += (char)c;
                }
        }
    }
    return result;
}

std::string SimpleJSON::unescape(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            switch (str[i + 1]) {
                case '"': result += '"'; i++; break;
                case '\\': result += '\\'; i++; break;
                case 'b': result += '\b'; i++; break;
                case 'f': result += '\f'; i++; break;
                case 'n': result += '\n'; i++; break;
                case 'r': result += '\r'; i++; break;
                case 't': result += '\t'; i++; break;
                case '/': result += '/'; i++; break;
                case 'u': {
                    // Unicode escape: \uXXXX
                    if (i + 5 < str.length()) {
                        std::string hex = str.substr(i + 2, 4);
                        unsigned int codepoint = std::stoul(hex, nullptr, 16);
                        
                        // Check for surrogate pair (emoji)
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 11 < str.length() 
                            && str[i + 6] == '\\' && str[i + 7] == 'u') {
                            // High surrogate followed by low surrogate
                            std::string hex2 = str.substr(i + 8, 4);
                            unsigned int low = std::stoul(hex2, nullptr, 16);
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                // Combine surrogates into full codepoint
                                unsigned int fullCodepoint = 0x10000 + ((codepoint & 0x3FF) << 10) + (low & 0x3FF);
                                // Convert to UTF-8
                                if (fullCodepoint <= 0x7F) {
                                    result += (char)fullCodepoint;
                                } else if (fullCodepoint <= 0x7FF) {
                                    result += (char)(0xC0 | (fullCodepoint >> 6));
                                    result += (char)(0x80 | (fullCodepoint & 0x3F));
                                } else if (fullCodepoint <= 0xFFFF) {
                                    result += (char)(0xE0 | (fullCodepoint >> 12));
                                    result += (char)(0x80 | ((fullCodepoint >> 6) & 0x3F));
                                    result += (char)(0x80 | (fullCodepoint & 0x3F));
                                } else {
                                    result += (char)(0xF0 | (fullCodepoint >> 18));
                                    result += (char)(0x80 | ((fullCodepoint >> 12) & 0x3F));
                                    result += (char)(0x80 | ((fullCodepoint >> 6) & 0x3F));
                                    result += (char)(0x80 | (fullCodepoint & 0x3F));
                                }
                                i += 11; // Skip both \uXXXX sequences
                                break;
                            }
                        }
                        
                        // Single codepoint (not surrogate pair) - convert to UTF-8
                        if (codepoint <= 0x7F) {
                            result += (char)codepoint;
                        } else if (codepoint <= 0x7FF) {
                            result += (char)(0xC0 | (codepoint >> 6));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            result += (char)(0xE0 | (codepoint >> 12));
                            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        }
                        i += 5; // Skip \uXXXX
                    } else {
                        result += str[i];
                    }
                    break;
                }
                default: result += str[i];
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string SimpleJSON::buildObject(const std::map<std::string, std::string>& obj) {
    std::string result = "{";
    bool first = true;
    for (const auto& pair : obj) {
        if (!first) result += ",";
        result += "\"" + pair.first + "\":\"" + escape(pair.second) + "\"";
        first = false;
    }
    result += "}";
    return result;
}

std::map<std::string, std::string> SimpleJSON::parseObject(const std::string& json) {
    std::map<std::string, std::string> result;
    
    size_t pos = json.find('{');
    if (pos == std::string::npos) return result;
    
    size_t end = json.rfind('}');
    if (end == std::string::npos) return result;
    
    std::string content = json.substr(pos + 1, end - pos - 1);
    
    // Simple parser - finds "key":"value" pairs
    size_t i = 0;
    while (i < content.length()) {
        // Skip whitespace
        while (i < content.length() && (content[i] == ' ' || content[i] == '\n' || content[i] == '\r' || content[i] == '\t')) i++;
        if (i >= content.length()) break;
        
        // Find key
        if (content[i] == '"') {
            i++;
            size_t keyStart = i;
            while (i < content.length() && content[i] != '"') i++;
            std::string key = content.substr(keyStart, i - keyStart);
            i++; // skip closing "
            
            // Skip to :
            while (i < content.length() && content[i] != ':') i++;
            i++; // skip :
            
            // Skip whitespace
            while (i < content.length() && (content[i] == ' ' || content[i] == '\n' || content[i] == '\r' || content[i] == '\t')) i++;
            
            // Get value
            if (i < content.length() && content[i] == '"') {
                i++;
                size_t valueStart = i;
                while (i < content.length() && content[i] != '"') {
                    if (content[i] == '\\') i++; // skip escaped char
                    i++;
                }
                std::string value = unescape(content.substr(valueStart, i - valueStart));
                result[key] = value;
                i++; // skip closing "
            }
            
            // Skip to comma or end
            while (i < content.length() && content[i] != ',' && content[i] != '}') i++;
            if (i < content.length() && content[i] == ',') i++;
        } else {
            i++;
        }
    }
    
    return result;
}

std::string SimpleJSON::getString(const std::map<std::string, std::string>& obj, const std::string& key) {
    auto it = obj.find(key);
    return (it != obj.end()) ? it->second : "";
}

std::vector<std::map<std::string, std::string>> SimpleJSON::parseArray(const std::string& json) {
    std::vector<std::map<std::string, std::string>> result;
    
    size_t pos = json.find('[');
    if (pos == std::string::npos) return result;
    
    size_t end = json.rfind(']');
    if (end == std::string::npos) return result;
    
    std::string content = json.substr(pos + 1, end - pos - 1);
    
    // Find objects in array
    int depth = 0;
    size_t objStart = 0;
    bool inString = false;
    
    for (size_t i = 0; i < content.length(); i++) {
        if (content[i] == '"' && (i == 0 || content[i-1] != '\\')) {
            inString = !inString;
        }
        if (!inString) {
            if (content[i] == '{') {
                if (depth == 0) objStart = i;
                depth++;
            } else if (content[i] == '}') {
                depth--;
                if (depth == 0) {
                    std::string obj = content.substr(objStart, i - objStart + 1);
                    result.push_back(parseObject(obj));
                }
            }
        }
    }
    
    return result;
}

std::string SimpleJSON::buildConversationArray(const std::vector<std::map<std::string, std::string>>& conversation) {
    std::string result = "[";
    for (size_t i = 0; i < conversation.size(); i++) {
        if (i > 0) result += ",";
        result += buildObject(conversation[i]);
    }
    result += "]";
    return result;
}
