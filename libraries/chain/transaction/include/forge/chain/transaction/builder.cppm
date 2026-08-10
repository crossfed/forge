module;

#include <vector>

export module forge.chain.transaction.builder;

export import forge.chain.transaction.exceptions;
export import forge.chain.transaction.types;

export namespace forge::chain::transaction {

class builder {
 public:
   explicit builder(context context, options options = {});

   builder& add_action(chain::protocol::action value);
   builder& add_context_free_action(chain::protocol::action value);
   builder& add_context_free_data(chain::protocol::bytes value);
   builder& add_extension(chain::protocol::transaction_extension value);

   [[nodiscard]] const context& chain_context() const noexcept;
   [[nodiscard]] const options& build_options() const noexcept;
   [[nodiscard]] unsigned_transaction build() const;

 private:
   context context_;
   options options_;
   std::vector<chain::protocol::action> actions_;
   std::vector<chain::protocol::action> context_free_actions_;
   std::vector<chain::protocol::bytes> context_free_data_;
   std::vector<chain::protocol::transaction_extension> extensions_;
};

[[nodiscard]] std::uint16_t reference_block_num(const chain::protocol::block_id& value) noexcept;
[[nodiscard]] std::uint32_t reference_block_prefix(const chain::protocol::block_id& value) noexcept;

} // namespace forge::chain::transaction
