#ifndef HTTP_CLIENT_WRAPPER_H
#define HTTP_CLIENT_WRAPPER_H

#include <HTTPClient.h>

class ScopedHttpClient {
private:
    HTTPClient& client;
    bool isActive;

public:
    ScopedHttpClient(HTTPClient& c) : client(c), isActive(false) {}
    
    ~ScopedHttpClient() {
        if (isActive) {
            client.end();
        }
    }
    
    bool begin(const String& url) {
        isActive = client.begin(url);
        return isActive;
    }
    
    HTTPClient& get() {
        return client;
    }
    
    operator bool() const {
        return isActive;
    }
};

#endif // HTTP_CLIENT_WRAPPER_H 