export module forge.chain.protocol.native_ids;

export import forge.db.ids.typed_id;

export namespace forge::chain::protocol {

using account_id = forge::db::ids::typed_id<1, 10>;
using metadata_id = forge::db::ids::typed_id<1, 11>;
using account_ram_correction_id = forge::db::ids::typed_id<1, 12>;
using code_id = forge::db::ids::typed_id<1, 13>;

using permission_usage_id = forge::db::ids::typed_id<1, 20>;
using permission_id = forge::db::ids::typed_id<1, 21>;
using permission_link_id = forge::db::ids::typed_id<1, 22>;

using table_id = forge::db::ids::typed_id<1, 30>;
using generated_transaction_id = forge::db::ids::typed_id<1, 51>;

using resource_limits_id = forge::db::ids::typed_id<1, 60>;
using resource_usage_id = forge::db::ids::typed_id<1, 61>;
using resource_config_id = forge::db::ids::typed_id<1, 62>;
using resource_state_id = forge::db::ids::typed_id<1, 63>;

} // namespace forge::chain::protocol
