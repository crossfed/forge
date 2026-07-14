#pragma once

#define FORGE_DB_OBJECT(index_type)                                                                                    \
   namespace forge::db::object {                                                                                       \
   template <>                                                                                                         \
   struct index_for_id<typename index_type::id_t> {                                                                    \
      using type = index_type;                                                                                         \
   };                                                                                                                   \
   }
