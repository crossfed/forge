module;

export module forge.auth.workload.subject_token;

export import forge.crypto.core.secret_string;

export namespace forge::auth::workload {

class subject_token {
 public:
   explicit subject_token(forge::crypto::core::secret_string secret);
   ~subject_token();

   subject_token(const subject_token&) = delete;
   subject_token& operator=(const subject_token&) = delete;
   subject_token(subject_token&&) noexcept;
   subject_token& operator=(subject_token&&) noexcept;

   [[nodiscard]] const forge::crypto::core::secret_string& secret() const noexcept;

 private:
   forge::crypto::core::secret_string secret_;
};

} // namespace forge::auth::workload
