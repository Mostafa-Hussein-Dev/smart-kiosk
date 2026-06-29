#pragma once

#include <Arduino.h>

class SessionManager {
public:
    void setAuth(const String& token, const String& role, const String& uid);
    void clearAuth();
    bool isAuthenticated();
    void recordActivity();
    void checkTimeouts();

    String getToken();
    String getRole();
    String getSessionId();
    String getAuthenticatedUid();
    void   setSessionId(const String& id);

    void   setName(const String& name);
    String getName();

    // Returns the appropriate voice endpoint based on auth state
    String getVoiceEndpoint();

private:
    String _jwtToken;
    String _role;
    String _name;
    String _sessionId;
    String _authenticatedUid;
    unsigned long _tokenObtainedAt;
    unsigned long _lastActivityAt;
};
