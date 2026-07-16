#pragma once

namespace forge::db::mdbx::detail {

forge::db::core::record_page scan_records(
   MDBX_txn* transaction,
   MDBX_dbi family,
   const forge::db::core::record_range& range,
   const forge::db::core::page_request& request);

} // namespace forge::db::mdbx::detail
