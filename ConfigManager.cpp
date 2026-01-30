#include "KindroidBot.h"

// ============================================
// ConfigManager - Legacy single config support
// ============================================

bool ConfigManager::saveConfig(const BotConfig& config, const std::string& filename) {
    std::map<std::string, std::string> configMap;
    configMap["profileName"] = config.profileName;
    configMap["discordToken"] = config.discordToken;
    configMap["discordEnabled"] = config.discordEnabled ? "true" : "false";
    configMap["apiKey"] = config.apiKey;
    configMap["aiId"] = config.aiId;
    configMap["baseUrl"] = config.baseUrl;
    configMap["personaName"] = config.personaName;
    configMap["twitchUsername"] = config.twitchUsername;
    configMap["twitchOAuth"] = config.twitchOAuth;
    configMap["twitchChannel"] = config.twitchChannel;
    configMap["twitchEnabled"] = config.twitchEnabled ? "true" : "false";
    configMap["announceMessage"] = config.announceMessage;
    configMap["announceDiscordChannel"] = config.announceDiscordChannel;
    configMap["announceHours"] = std::to_string(config.announceHours);
    configMap["announceMins"] = std::to_string(config.announceMins);
    configMap["announceDiscord"] = config.announceDiscord ? "true" : "false";
    configMap["announceTwitch"] = config.announceTwitch ? "true" : "false";
    
    std::string json = SimpleJSON::buildObject(configMap);
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << json;
    file.close();
    return true;
}

bool ConfigManager::loadConfig(BotConfig& config, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    std::string json = buffer.str();
    std::map<std::string, std::string> configMap = SimpleJSON::parseObject(json);
    
    config.profileName = SimpleJSON::getString(configMap, "profileName");
    config.discordToken = SimpleJSON::getString(configMap, "discordToken");
    config.discordEnabled = SimpleJSON::getString(configMap, "discordEnabled") != "false"; // Default true
    config.apiKey = SimpleJSON::getString(configMap, "apiKey");
    config.aiId = SimpleJSON::getString(configMap, "aiId");
    config.baseUrl = SimpleJSON::getString(configMap, "baseUrl");
    config.personaName = SimpleJSON::getString(configMap, "personaName");
    config.twitchUsername = SimpleJSON::getString(configMap, "twitchUsername");
    config.twitchOAuth = SimpleJSON::getString(configMap, "twitchOAuth");
    config.twitchChannel = SimpleJSON::getString(configMap, "twitchChannel");
    config.twitchEnabled = SimpleJSON::getString(configMap, "twitchEnabled") == "true";
    config.announceMessage = SimpleJSON::getString(configMap, "announceMessage");
    config.announceDiscordChannel = SimpleJSON::getString(configMap, "announceDiscordChannel");
    std::string hoursStr = SimpleJSON::getString(configMap, "announceHours");
    std::string minsStr = SimpleJSON::getString(configMap, "announceMins");
    config.announceHours = hoursStr.empty() ? 0 : std::stoi(hoursStr);
    config.announceMins = minsStr.empty() ? 30 : std::stoi(minsStr);
    config.announceDiscord = SimpleJSON::getString(configMap, "announceDiscord") == "true";
    config.announceTwitch = SimpleJSON::getString(configMap, "announceTwitch") == "true";
    
    if (config.baseUrl.empty()) {
        config.baseUrl = "https://api.kindroid.ai/v1";
    }
    if (config.personaName.empty()) {
        config.personaName = "User";
    }
    
    return !config.apiKey.empty();
}

// ============================================
// ProfileManager - Multiple profile support
// ============================================

std::string ProfileManager::buildProfilesJson(const std::vector<BotConfig>& profiles) {
    std::string json = "[\n";
    
    for (size_t i = 0; i < profiles.size(); i++) {
        const BotConfig& p = profiles[i];
        
        json += "  {\n";
        json += "    \"profileName\": \"" + SimpleJSON::escape(p.profileName) + "\",\n";
        json += "    \"discordToken\": \"" + SimpleJSON::escape(p.discordToken) + "\",\n";
        json += "    \"discordEnabled\": \"" + std::string(p.discordEnabled ? "true" : "false") + "\",\n";
        json += "    \"apiKey\": \"" + SimpleJSON::escape(p.apiKey) + "\",\n";
        json += "    \"aiId\": \"" + SimpleJSON::escape(p.aiId) + "\",\n";
        json += "    \"baseUrl\": \"" + SimpleJSON::escape(p.baseUrl) + "\",\n";
        json += "    \"personaName\": \"" + SimpleJSON::escape(p.personaName) + "\",\n";
        json += "    \"twitchUsername\": \"" + SimpleJSON::escape(p.twitchUsername) + "\",\n";
        json += "    \"twitchOAuth\": \"" + SimpleJSON::escape(p.twitchOAuth) + "\",\n";
        json += "    \"twitchChannel\": \"" + SimpleJSON::escape(p.twitchChannel) + "\",\n";
        json += "    \"twitchEnabled\": \"" + std::string(p.twitchEnabled ? "true" : "false") + "\",\n";
        json += "    \"announceMessage\": \"" + SimpleJSON::escape(p.announceMessage) + "\",\n";
        json += "    \"announceDiscordChannel\": \"" + SimpleJSON::escape(p.announceDiscordChannel) + "\",\n";
        json += "    \"announceHours\": \"" + std::to_string(p.announceHours) + "\",\n";
        json += "    \"announceMins\": \"" + std::to_string(p.announceMins) + "\",\n";
        json += "    \"announceDiscord\": \"" + std::string(p.announceDiscord ? "true" : "false") + "\",\n";
        json += "    \"announceTwitch\": \"" + std::string(p.announceTwitch ? "true" : "false") + "\"\n";
        json += "  }";
        
        if (i < profiles.size() - 1) {
            json += ",";
        }
        json += "\n";
    }
    
    json += "]";
    return json;
}

std::vector<BotConfig> ProfileManager::parseProfilesJson(const std::string& json) {
    std::vector<BotConfig> profiles;
    
    // Find array bounds
    size_t arrayStart = json.find('[');
    size_t arrayEnd = json.rfind(']');
    
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
        return profiles;
    }
    
    // Parse each object in the array
    std::string content = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
    
    int depth = 0;
    size_t objStart = 0;
    bool inString = false;
    
    for (size_t i = 0; i < content.length(); i++) {
        char c = content[i];
        
        // Handle string state
        if (c == '"' && (i == 0 || content[i-1] != '\\')) {
            inString = !inString;
            continue;
        }
        
        if (!inString) {
            if (c == '{') {
                if (depth == 0) {
                    objStart = i;
                }
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    // Extract and parse this object
                    std::string objStr = content.substr(objStart, i - objStart + 1);
                    std::map<std::string, std::string> obj = SimpleJSON::parseObject(objStr);
                    
                    BotConfig profile;
                    profile.profileName = SimpleJSON::getString(obj, "profileName");
                    profile.discordToken = SimpleJSON::getString(obj, "discordToken");
                    profile.discordEnabled = SimpleJSON::getString(obj, "discordEnabled") != "false"; // Default true
                    profile.apiKey = SimpleJSON::getString(obj, "apiKey");
                    profile.aiId = SimpleJSON::getString(obj, "aiId");
                    profile.baseUrl = SimpleJSON::getString(obj, "baseUrl");
                    profile.personaName = SimpleJSON::getString(obj, "personaName");
                    profile.twitchUsername = SimpleJSON::getString(obj, "twitchUsername");
                    profile.twitchOAuth = SimpleJSON::getString(obj, "twitchOAuth");
                    profile.twitchChannel = SimpleJSON::getString(obj, "twitchChannel");
                    profile.twitchEnabled = SimpleJSON::getString(obj, "twitchEnabled") == "true";
                    profile.announceMessage = SimpleJSON::getString(obj, "announceMessage");
                    profile.announceDiscordChannel = SimpleJSON::getString(obj, "announceDiscordChannel");
                    std::string hoursStr = SimpleJSON::getString(obj, "announceHours");
                    std::string minsStr = SimpleJSON::getString(obj, "announceMins");
                    profile.announceHours = hoursStr.empty() ? 0 : std::stoi(hoursStr);
                    profile.announceMins = minsStr.empty() ? 30 : std::stoi(minsStr);
                    profile.announceDiscord = SimpleJSON::getString(obj, "announceDiscord") == "true";
                    profile.announceTwitch = SimpleJSON::getString(obj, "announceTwitch") == "true";
                    
                    if (profile.baseUrl.empty()) {
                        profile.baseUrl = "https://api.kindroid.ai/v1";
                    }
                    if (profile.personaName.empty()) {
                        profile.personaName = "User";
                    }
                    
                    // Only add if it has a name
                    if (!profile.profileName.empty()) {
                        profiles.push_back(profile);
                    }
                }
            }
        }
    }
    
    return profiles;
}

std::vector<BotConfig> ProfileManager::loadAllProfiles(const std::string& filename) {
    std::vector<BotConfig> profiles;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        return profiles;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    profiles = parseProfilesJson(buffer.str());
    return profiles;
}

bool ProfileManager::saveAllProfiles(const std::vector<BotConfig>& profiles, const std::string& filename) {
    std::string json = buildProfilesJson(profiles);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << json;
    file.close();
    return true;
}

BotConfig* ProfileManager::findProfile(std::vector<BotConfig>& profiles, const std::string& profileName) {
    for (auto& p : profiles) {
        if (p.profileName == profileName) {
            return &p;
        }
    }
    return nullptr;
}

bool ProfileManager::addOrUpdateProfile(const BotConfig& profile, const std::string& filename) {
    std::vector<BotConfig> profiles = loadAllProfiles(filename);
    
    // Find existing profile with same name
    bool found = false;
    for (auto& p : profiles) {
        if (p.profileName == profile.profileName) {
            // Update existing
            p = profile;
            found = true;
            break;
        }
    }
    
    if (!found) {
        // Add new profile
        profiles.push_back(profile);
    }
    
    return saveAllProfiles(profiles, filename);
}

bool ProfileManager::deleteProfile(const std::string& profileName, const std::string& filename) {
    std::vector<BotConfig> profiles = loadAllProfiles(filename);
    
    // Find and remove
    auto it = std::remove_if(profiles.begin(), profiles.end(),
        [&profileName](const BotConfig& p) {
            return p.profileName == profileName;
        });
    
    if (it == profiles.end()) {
        return false; // Not found
    }
    
    profiles.erase(it, profiles.end());
    return saveAllProfiles(profiles, filename);
}
