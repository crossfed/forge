module;

export module forge.crypto.keystore.encrypted_file;

export import forge.crypto.keystore.exceptions;
export import forge.crypto.keystore.types;

export namespace forge::crypto::keystore {

[[nodiscard]] core::bytes encrypt_file(encrypted_file_request request);
[[nodiscard]] core::secret_bytes decrypt_file(const core::bytes& container, const core::secret_string& password,
                                              decrypt_limits limits = {});

} // namespace forge::crypto::keystore
