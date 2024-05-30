// TokenHelper.h
#ifndef TOKEN_HELPER_H
#define TOKEN_HELPER_H

#include <Firebase_ESP_Client.h>

String getTokenType(TokenInfo info) {
  switch (info.type) {
    case token_type_undefined:
      return "undefined";
    case token_type_legacy_token:
      return "legacy token";
    case token_type_id_token:
      return "ID token";
    case token_type_custom_token:
      return "custom token";
    case token_type_oauth2_access_token:
      return "OAuth2.0 access token";
    default:
      return "unknown";
  }
}

String getTokenStatus(TokenInfo info) {
  switch (info.status) {
    case token_status_uninitialized:
      return "uninitialized";
    case token_status_on_signing:
      return "on signing";
    case token_status_on_request:
      return "on request";
    case token_status_on_refresh:
      return "on refresh";
    case token_status_ready:
      return "ready";
    case token_status_error:
      return "error";
    default:
      return "unknown";
  }
}

#endif // TOKEN_HELPER_H
