export module forge.api.transport.exceptions;

export import forge.api.core.exceptions;

export namespace forge::api::transport::exceptions {

using cancelled = forge::api::core::exceptions::cancelled;
using codec_failed = forge::api::core::exceptions::codec_failed;
using deadline_exceeded = forge::api::core::exceptions::deadline_exceeded;
using protocol_error = forge::api::core::exceptions::protocol_error;
using resource_exhausted = forge::api::core::exceptions::resource_exhausted;

} // namespace forge::api::transport::exceptions
