#pragma once

#define FORGE_DB_OBJECT(object_index_type)                                                                             \
   namespace forge::ids {                                                                                              \
   template <>                                                                                                          \
   struct type_for_id<typename object_index_type::id_t> {                                                               \
      using type = object_index_type;                                                                                   \
   };                                                                                                                   \
   }
