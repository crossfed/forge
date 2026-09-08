#include "details/appauth_backend.hxx"
#include "details/auth_state_binding.hxx"

#import <AppAuth.h>
#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <dispatch/dispatch.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

@interface FORGEAppAuthStateEnvelope : NSObject <NSSecureCoding>

@property(nonatomic, readonly) OIDAuthState* authState;
@property(nonatomic, readonly) NSString* binding;

- (instancetype)initWithAuthState:(OIDAuthState*)authState binding:(NSString*)binding;

@end

@implementation FORGEAppAuthStateEnvelope

+ (BOOL)supportsSecureCoding {
   return YES;
}

- (instancetype)initWithAuthState:(OIDAuthState*)authState binding:(NSString*)binding {
   self = [super init];
   if (self != nil) {
      _authState = authState;
      _binding = [binding copy];
   }
   return self;
}

- (void)encodeWithCoder:(NSCoder*)coder {
   [coder encodeObject:_authState forKey:@"auth_state"];
   [coder encodeObject:_binding forKey:@"binding"];
}

- (instancetype)initWithCoder:(NSCoder*)coder {
   OIDAuthState* authState = [coder decodeObjectOfClass:[OIDAuthState class] forKey:@"auth_state"];
   NSString* binding = [coder decodeObjectOfClass:[NSString class] forKey:@"binding"];
   if (authState == nil || binding == nil || binding.length == 0U) {
      return nil;
   }
   return [self initWithAuthState:authState binding:binding];
}

@end

namespace forge::auth::appauth::detail {
namespace {

void secure_erase(std::string& value) noexcept {
   volatile char* data = value.empty() ? nullptr : value.data();
   for (std::size_t index = 0; data != nullptr && index < value.size(); ++index) {
      data[index] = '\0';
   }
   value.clear();
}

void dispatch_main(std::function<void()> operation) {
   if ([NSThread isMainThread]) {
      operation();
      return;
   }
   dispatch_async(dispatch_get_main_queue(), ^{
     operation();
   });
}

class operation final : public backend_operation {
 public:
   void cancel() noexcept override {
      try {
         auto action = std::function<void()>{};
         {
            const auto lock = std::scoped_lock{mutex_};
            canceled_ = true;
            action = std::move(cancel_action_);
         }
         if (action) {
            dispatch_main(std::move(action));
         }
      } catch (...) {
      }
   }

   [[nodiscard]] bool canceled() const noexcept {
      const auto lock = std::scoped_lock{mutex_};
      return canceled_;
   }

   void set_cancel_action(std::function<void()> action) {
      auto invoke = std::function<void()>{};
      {
         const auto lock = std::scoped_lock{mutex_};
         if (canceled_) {
            invoke = std::move(action);
         } else {
            cancel_action_ = std::move(action);
         }
      }
      if (invoke) {
         dispatch_main(std::move(invoke));
      }
   }

   void finish() noexcept {
      try {
         const auto lock = std::scoped_lock{mutex_};
         cancel_action_ = {};
      } catch (...) {
      }
   }

 private:
   mutable std::mutex mutex_;
   std::function<void()> cancel_action_;
   bool canceled_ = false;
};

void complete(backend_completion completion, backend_error error) {
   completion(backend_result{.error = error});
}

[[nodiscard]] OIDExternalUserAgentMac* system_browser_agent() {
   // AppAuth uses this initializer specifically to select the system browser without an AppKit window.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
   auto agent = [[OIDExternalUserAgentMac alloc] init];
#pragma clang diagnostic pop
   return agent;
}

[[nodiscard]] NSString* ns_string(const std::string& value) {
   return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}

[[nodiscard]] NSURL* https_url(const std::string& value) {
   auto result = [NSURL URLWithString:ns_string(value)];
   if (result == nil || ![[result scheme] isEqualToString:@"https"] || [result host] == nil) {
      return nil;
   }
   return result;
}

[[nodiscard]] NSArray<NSString*>* ns_scopes(const std::vector<std::string>& scopes) {
   auto result = [[NSMutableArray<NSString*> alloc] initWithCapacity:scopes.size()];
   for (const auto& scope : scopes) {
      auto text = ns_string(scope);
      if (text == nil) {
         return nil;
      }
      [result addObject:text];
   }
   return result;
}

[[nodiscard]] NSDictionary<NSString*, NSString*>* audience_parameters(const backend_options& options) {
   if (!options.audience.has_value()) {
      return nil;
   }
   auto audience = ns_string(*options.audience);
   return audience == nil ? nil : @{@"audience" : audience};
}

[[nodiscard]] auth_state_config state_config(const backend_options& options) {
   return {
       .issuer = options.issuer,
       .client_id = options.client_id,
       .scopes = options.scopes,
       .audience = options.audience,
   };
}

[[nodiscard]] NSString* state_binding(const backend_options& options) {
   return ns_string(canonical_auth_state_binding(state_config(options)));
}

[[nodiscard]] NSMutableDictionary* keychain_query(const backend_options& options) {
   auto service = ns_string(options.keychain_service);
   auto account = ns_string(options.keychain_account);
   if (service == nil || account == nil) {
      return nil;
   }
   return [@{
      (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService : service,
      (__bridge id)kSecAttrAccount : account,
   } mutableCopy];
}

[[nodiscard]] backend_error erase_auth_state(const backend_options& options) {
   auto query = keychain_query(options);
   if (query == nil) {
      return backend_error::invalid_options;
   }
   const auto status = SecItemDelete((__bridge CFDictionaryRef)query);
   return status == errSecSuccess || status == errSecItemNotFound ? backend_error::none
                                                                  : backend_error::keychain_failure;
}

[[nodiscard]] backend_error store_auth_state(const backend_options& options, OIDAuthState* state) {
   auto binding = state_binding(options);
   if (binding == nil) {
      return backend_error::invalid_options;
   }

   FORGEAppAuthStateEnvelope* envelope = [[FORGEAppAuthStateEnvelope alloc] initWithAuthState:state binding:binding];
   if (envelope == nil) {
      return backend_error::keychain_failure;
   }

   NSError* archive_error = nil;
   auto data = [NSKeyedArchiver archivedDataWithRootObject:envelope requiringSecureCoding:YES error:&archive_error];
   if (data == nil || archive_error != nil) {
      return backend_error::keychain_failure;
   }

   auto query = keychain_query(options);
   if (query == nil) {
      return backend_error::invalid_options;
   }
   auto attributes = @{(__bridge id)kSecValueData : data};
   auto status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
   if (status == errSecItemNotFound) {
      [query addEntriesFromDictionary:@{
         (__bridge id)kSecValueData : data,
         (__bridge id)kSecAttrAccessible : (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
      }];
      status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
   }
   return status == errSecSuccess ? backend_error::none : backend_error::keychain_failure;
}

struct loaded_auth_state {
   OIDAuthState* value = nil;
   backend_error error = backend_error::none;
   bool erase_required = false;
};

[[nodiscard]] loaded_auth_state load_auth_state(const backend_options& options) {
   auto query = keychain_query(options);
   if (query == nil) {
      return {.error = backend_error::invalid_options};
   }
   query[(__bridge id)kSecReturnData] = @YES;
   query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;

   CFTypeRef value = nullptr;
   const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &value);
   if (status == errSecItemNotFound) {
      return {};
   }
   if (status != errSecSuccess || value == nullptr) {
      return {.error = backend_error::keychain_failure};
   }

   NSData* data = CFBridgingRelease(value);
   NSError* decode_error = nil;
   FORGEAppAuthStateEnvelope* envelope = [NSKeyedUnarchiver unarchivedObjectOfClass:[FORGEAppAuthStateEnvelope class]
                                                                           fromData:data
                                                                              error:&decode_error];
   NSString* binding = state_binding(options);
   if (envelope == nil || binding == nil || ![envelope.binding isEqualToString:binding] || decode_error != nil) {
      return {.error = backend_error::setup_required, .erase_required = true};
   }
   return {.value = envelope.authState};
}

[[nodiscard]] bool erase_if_current(auth_state_epoch& epoch, auth_state_epoch::ticket expected,
                                    const backend_options& options, backend_error& error) {
   return epoch.commit_if_current(expected, [&] { error = erase_auth_state(options); });
}

[[nodiscard]] backend_result make_token_result(NSString* token, NSDate* expires_at, const backend_options& options,
                                               std::chrono::seconds minimum_validity) {
   if (token == nil || [token length] == 0U) {
      return {.error = backend_error::token_unavailable};
   }
   if (expires_at == nil && minimum_validity.count() != 0) {
      return {.error = backend_error::token_unavailable};
   }
   if (expires_at != nil && [expires_at timeIntervalSinceNow] < static_cast<NSTimeInterval>(minimum_validity.count())) {
      return {.error = backend_error::token_unavailable};
   }

   auto encoded = [token dataUsingEncoding:NSUTF8StringEncoding];
   if (encoded == nil || [encoded length] == 0U) {
      return {.error = backend_error::token_unavailable};
   }
   auto raw = std::string{static_cast<const char*>([encoded bytes]), [encoded length]};
   if (raw.find('\0') != std::string::npos) {
      secure_erase(raw);
      return {.error = backend_error::token_unavailable};
   }

   auto result = backend_result{};
   result.token.emplace(token_value{
       .token = secret_value{std::move(raw)},
       .issuer = options.issuer,
   });
   if (expires_at != nil) {
      const auto seconds = static_cast<std::int64_t>([expires_at timeIntervalSince1970]);
      result.token->expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
   }
   return result;
}

class appauth_backend_impl final : public appauth_backend, public std::enable_shared_from_this<appauth_backend_impl> {
 public:
   explicit appauth_backend_impl(backend_options options) : options_{std::move(options)} {}

   [[nodiscard]] std::shared_ptr<backend_operation> begin_authorize(backend_completion completion) override {
      auto active = std::make_shared<operation>();
      auto self = shared_from_this();
      const auto expected_epoch = state_epoch_.capture();
      dispatch_main([self, active, expected_epoch, completion = std::move(completion)]() mutable {
         @autoreleasepool {
            if (active->canceled()) {
               complete(std::move(completion), backend_error::canceled);
               return;
            }
            auto issuer = https_url(self->options_.issuer);
            if (issuer == nil) {
               complete(std::move(completion), backend_error::invalid_options);
               return;
            }
            [OIDAuthorizationService
                discoverServiceConfigurationForIssuer:issuer
                                           completion:^(OIDServiceConfiguration* configuration,
                                                        NSError* discovery_error) {
                                             dispatch_main([self, active, expected_epoch, configuration,
                                                            discovery_error, completion]() mutable {
                                                @autoreleasepool {
                                                   if (active->canceled()) {
                                                      complete(std::move(completion), backend_error::canceled);
                                                      return;
                                                   }
                                                   if (configuration == nil || discovery_error != nil) {
                                                      complete(std::move(completion), backend_error::setup_required);
                                                      return;
                                                   }

                                                   auto success_url =
                                                       https_url(self->options_.authorization_success_url);
                                                   auto client_id = ns_string(self->options_.client_id);
                                                   auto scopes = ns_scopes(self->options_.scopes);
                                                   auto parameters = audience_parameters(self->options_);
                                                   if (success_url == nil || client_id == nil || scopes == nil ||
                                                       (self->options_.audience.has_value() && parameters == nil)) {
                                                      complete(std::move(completion), backend_error::invalid_options);
                                                      return;
                                                   }

                                                   auto handler =
                                                       [[OIDRedirectHTTPHandler alloc] initWithSuccessURL:success_url];
                                                   NSError* listener_error = nil;
                                                   auto redirect = [handler startHTTPListener:&listener_error];
                                                   if (redirect == nil || listener_error != nil) {
                                                      complete(std::move(completion), backend_error::token_unavailable);
                                                      return;
                                                   }

                                                   auto request = [[OIDAuthorizationRequest alloc]
                                                       initWithConfiguration:configuration
                                                                    clientId:client_id
                                                                      scopes:scopes
                                                                 redirectURL:redirect
                                                                responseType:OIDResponseTypeCode
                                                        additionalParameters:parameters];
                                                   if (request == nil) {
                                                      [handler cancelHTTPListener];
                                                      complete(std::move(completion), backend_error::token_unavailable);
                                                      return;
                                                   }
                                                   auto agent = system_browser_agent();
                                                   auto flow = [OIDAuthState
                                                       authStateByPresentingAuthorizationRequest:request
                                                                               externalUserAgent:agent
                                                                                        callback:^(
                                                                                            OIDAuthState* auth_state,
                                                                                            NSError*
                                                                                                authorization_error) {
                                                                                          static_cast<void>(handler);
                                                                                          static_cast<void>(agent);
                                                                                          active->finish();
                                                                                          if (active->canceled()) {
                                                                                             complete(
                                                                                                 std::move(completion),
                                                                                                 backend_error::
                                                                                                     canceled);
                                                                                             return;
                                                                                          }
                                                                                          if (auth_state == nil ||
                                                                                              authorization_error !=
                                                                                                  nil) {
                                                                                             complete(
                                                                                                 std::move(completion),
                                                                                                 backend_error::
                                                                                                     setup_required);
                                                                                             return;
                                                                                          }
                                                                                          auto store_error =
                                                                                              backend_error::none;
                                                                                          if (!self->state_epoch_.commit_if_current(
                                                                                                  expected_epoch, [&] {
                                                                                                     store_error =
                                                                                                         store_auth_state(
                                                                                                             self->options_,
                                                                                                             auth_state);
                                                                                                  })) {
                                                                                             complete(
                                                                                                 std::move(completion),
                                                                                                 backend_error::
                                                                                                     canceled);
                                                                                             return;
                                                                                          }
                                                                                          complete(
                                                                                              std::move(completion),
                                                                                              store_error);
                                                                                        }];
                                                   if (flow == nil) {
                                                      [handler cancelHTTPListener];
                                                      complete(std::move(completion), backend_error::token_unavailable);
                                                      return;
                                                   }
                                                   handler.currentAuthorizationFlow = flow;
                                                   active->set_cancel_action([handler, flow] {
                                                      [handler cancelHTTPListener];
                                                      [flow cancel];
                                                   });
                                                }
                                             });
                                           }];
         }
      });
      return active;
   }

   [[nodiscard]] std::shared_ptr<backend_operation> begin_invalidate(backend_completion completion) override {
      auto active = std::make_shared<operation>();
      auto self = shared_from_this();
      const auto erase_error = self->state_epoch_.invalidate([self] { return erase_auth_state(self->options_); });
      dispatch_main([active, erase_error, completion = std::move(completion)]() mutable {
         @autoreleasepool {
            complete(std::move(completion), active->canceled() ? backend_error::canceled : erase_error);
         }
      });
      return active;
   }

   [[nodiscard]] std::shared_ptr<backend_operation> begin_access_token(std::chrono::seconds minimum_validity,
                                                                       backend_completion completion) override {
      auto active = std::make_shared<operation>();
      auto self = shared_from_this();
      const auto expected_epoch = state_epoch_.capture();
      dispatch_main([self, active, expected_epoch, minimum_validity, completion = std::move(completion)]() mutable {
         @autoreleasepool {
            if (active->canceled()) {
               complete(std::move(completion), backend_error::canceled);
               return;
            }
            auto loaded = load_auth_state(self->options_);
            if (loaded.error != backend_error::none) {
               if (loaded.erase_required) {
                  auto erase_error = backend_error::none;
                  if (!erase_if_current(self->state_epoch_, expected_epoch, self->options_, erase_error)) {
                     complete(std::move(completion), backend_error::canceled);
                     return;
                  }
                  complete(std::move(completion),
                           erase_error == backend_error::none ? backend_error::setup_required : erase_error);
                  return;
               }
               complete(std::move(completion), loaded.error);
               return;
            }
            if (loaded.value == nil || !loaded.value.isAuthorized) {
               auto erase_error = backend_error::none;
               if (!erase_if_current(self->state_epoch_, expected_epoch, self->options_, erase_error)) {
                  complete(std::move(completion), backend_error::canceled);
                  return;
               }
               complete(std::move(completion),
                        erase_error == backend_error::none ? backend_error::setup_required : erase_error);
               return;
            }

            auto expires_at = loaded.value.lastTokenResponse.accessTokenExpirationDate;
            if (minimum_validity.count() != 0 &&
                (expires_at == nil || [expires_at timeIntervalSinceNow] < minimum_validity.count())) {
               [loaded.value setNeedsTokenRefresh];
            }
            [loaded.value
                performActionWithFreshTokens:^(NSString* token, NSString*, NSError* token_error) {
                  if (active->canceled()) {
                     complete(std::move(completion), backend_error::canceled);
                     return;
                  }
                  if (token == nil || token_error != nil) {
                     auto erase_error = backend_error::none;
                     if (!erase_if_current(self->state_epoch_, expected_epoch, self->options_, erase_error)) {
                        complete(std::move(completion), backend_error::canceled);
                        return;
                     }
                     complete(std::move(completion),
                              erase_error == backend_error::none ? backend_error::setup_required : erase_error);
                     return;
                  }
                  auto store_error = backend_error::none;
                  if (!self->state_epoch_.commit_if_current(
                          expected_epoch, [&] { store_error = store_auth_state(self->options_, loaded.value); })) {
                     complete(std::move(completion), backend_error::canceled);
                     return;
                  }
                  if (store_error != backend_error::none) {
                     complete(std::move(completion), store_error);
                     return;
                  }
                  completion(make_token_result(token, loaded.value.lastTokenResponse.accessTokenExpirationDate,
                                               self->options_, minimum_validity));
                }
                 additionalRefreshParameters:audience_parameters(self->options_)];
         }
      });
      return active;
   }

 private:
   backend_options options_;
   auth_state_epoch state_epoch_;
};

} // namespace

secret_value::secret_value(std::string value) noexcept {
   value_.swap(value);
   secure_erase(value);
}

secret_value::~secret_value() {
   secure_erase(value_);
}

secret_value::secret_value(secret_value&& other) noexcept {
   value_.swap(other.value_);
   secure_erase(other.value_);
}

secret_value& secret_value::operator=(secret_value&& other) noexcept {
   if (this != &other) {
      secure_erase(value_);
      value_.swap(other.value_);
      secure_erase(other.value_);
   }
   return *this;
}

std::string secret_value::take() && noexcept {
   auto result = std::string{};
   result.swap(value_);
   return result;
}

std::shared_ptr<appauth_backend> make_appauth_backend(backend_options options) {
   return std::make_shared<appauth_backend_impl>(std::move(options));
}

} // namespace forge::auth::appauth::detail
