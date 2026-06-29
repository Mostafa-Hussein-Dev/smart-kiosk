#include "session_manager.h"
#include "config.h"

void SessionManager::setAuth(const String& token, const String& role, const String& uid) {
    _jwtToken = token;
    _role = role;
    _authenticatedUid = uid;
    _sessionId = "";  // new auth session, clear old chat session
    _tokenObtainedAt = millis();
    _lastActivityAt = millis();
    Serial.printf("[Session] Authenticated as %s (UID: %s)\n", role.c_str(), uid.c_str());
}

void SessionManager::clearAuth() {
    if (_jwtToken.length() > 0) {
        Serial.println("[Session] Logged out.");
    }
    _jwtToken = "";
    _role = "";
    _name = "";
    _sessionId = "";
    _authenticatedUid = "";
    _tokenObtainedAt = 0;
    _lastActivityAt = 0;
}

void SessionManager::setName(const String& name) {
    _name = name;
}

String SessionManager::getName() {
    return _name;
}

bool SessionManager::isAuthenticated() {
    return _jwtToken.length() > 0;
}

void SessionManager::recordActivity() {
    _lastActivityAt = millis();
}

void SessionManager::checkTimeouts() {
    if (!isAuthenticated()) return;

    unsigned long now = millis();

    // Hard limit: token lifetime (15 min)
    if (now - _tokenObtainedAt > TOKEN_LIFETIME_MS) {
        Serial.println("[Session] Token expired (lifetime exceeded).");
        clearAuth();
        return;
    }

    // Soft limit: inactivity timeout (5 min)
    if (now - _lastActivityAt > INACTIVITY_TIMEOUT_MS) {
        Serial.println("[Session] Inactivity timeout.");
        clearAuth();
        return;
    }
}

String SessionManager::getToken() {
    return _jwtToken;
}

String SessionManager::getRole() {
    return _role;
}

String SessionManager::getSessionId() {
    return _sessionId;
}

String SessionManager::getAuthenticatedUid() {
    return _authenticatedUid;
}

void SessionManager::setSessionId(const String& id) {
    _sessionId = id;
}

String SessionManager::getVoiceEndpoint() {
    if (isAuthenticated()) {
        return String(BACKEND_URL) + "/api/voice/chat";
    }
    return String(BACKEND_URL) + "/api/voice/chat/public";
}
