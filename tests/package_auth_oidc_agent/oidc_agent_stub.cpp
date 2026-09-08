#include <ctime>

extern "C" {
#include <oidc-agent/api.h>
}

extern "C" agent_response getAgentTokenResponse(const char*, std::time_t, const char*, const char*, const char*) {
   return {};
}

extern "C" agent_response getAgentTokenResponseForIssuer(const char*, std::time_t, const char*, const char*,
                                                         const char*) {
   return {};
}

extern "C" void secFreeAgentResponse(agent_response) {}
