#pragma once

#include <Arduino.h>

// Result from RFID authentication API call
struct AuthResult {
    bool   success;    // True if authentication succeeded
    int    httpCode;   // HTTP status code
    String token;      // JWT access token
    String role;       // User role (student/admin)
    String name;       // User name
    String error;      // Error message if failed
};

// Result from voice chat API call
struct VoiceChatResult {
    bool    success;      // True if request succeeded
    int     httpCode;     // HTTP status code
    String  sessionId;    // Session ID from response header
    String  transcription;// Transcription from response header
    uint8_t* mp3Data;    // MP3 audio data (caller must free)
    size_t  mp3Length;    // Length of MP3 data
    String  error;        // Error message if failed
};

class ApiClient {
public:
    // Check if backend is reachable
    bool healthCheck();

    // Authenticate RFID tag with backend
    // Returns AuthResult with token if successful
    AuthResult postRfidAuth(const String& rfidTag);

    // Fetch the authenticated student's display name via /api/students/me/profile.
    // Returns "First Last", or "" if unavailable (e.g. non-student role).
    String getStudentName(const String& bearerToken);

    // Send voice chat request with WAV audio
    // wavData: WAV audio data with header (44 bytes) + PCM
    // wavLen: Total length of WAV data
    // sessionId: Current session ID (empty for new session)
    // bearerToken: Auth token (empty for public endpoint)
    // Returns VoiceChatResult with MP3 audio data
    VoiceChatResult postVoiceChat(
        const uint8_t* wavData,
        size_t wavLen,
        const String& sessionId,
        const String& bearerToken
    );

    // Send a typed-text chat request (mic replacement). The backend skips STT,
    // runs chat + TTS, and returns the same MP3 + headers as postVoiceChat.
    //   text: the question as a plain string
    //   sessionId: current session ID (empty for new session)
    //   bearerToken: auth token (empty -> public endpoint)
    VoiceChatResult postTextChat(
        const String& text,
        const String& sessionId,
        const String& bearerToken
    );
};
