module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <new>
#include <string>
#include <vector>

export module forge.chain.abi;

export import forge.chain.types;
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.containers;
import forge.variant.described;

export namespace forge::chain {


using type_name = std::string;
using field_name = std::string;

struct type_def {
   type_name new_type_name;
   type_name type;
};

struct field_def {
   field_name name;
   type_name type;
};

struct struct_def {
   type_name name;
   type_name base;
   std::vector<field_def> fields;
};

struct action_def {
   action_name name;
   type_name type;
   std::string ricardian_contract;
};

struct table_def {
   table_name name;
   type_name index_type;
   std::vector<field_name> key_names;
   std::vector<type_name> key_types;
   type_name type;
};

struct clause_pair {
   std::string id;
   std::string body;
};

struct error_message {
   std::uint64_t error_code = 0;
   std::string error_msg;
};

struct variant_def {
   type_name name;
   std::vector<type_name> types;
};

struct action_result_def {
   action_name name;
   type_name result_type;
};

template <typename T>
struct may_not_exist {
   T value{};
};

template <typename Stream, typename T>
Stream& operator<<(Stream& stream, const may_not_exist<T>& value) {
   forge::raw::pack(stream, value.value);
   return stream;
}

template <typename Stream, typename T>
Stream& operator>>(Stream& stream, may_not_exist<T>& value) {
   if constexpr (requires { stream.remaining(); }) {
      if (stream.remaining()) {
         forge::raw::unpack(stream, value.value);
      } else {
         value.value = T{};
      }
   } else {
      forge::raw::unpack(stream, value.value);
   }
   return stream;
}

template <typename T>
void to_variant(const may_not_exist<T>& value, forge::variant& variant) {
   forge::to_variant(value.value, variant);
}

template <typename T>
void from_variant(const forge::variant& variant, may_not_exist<T>& value) {
   forge::from_variant(variant, value.value);
}

struct abi_def {
   std::string version;
   std::vector<type_def> types;
   std::vector<struct_def> structs;
   std::vector<action_def> actions;
   std::vector<table_def> tables;
   std::vector<clause_pair> ricardian_clauses;
   std::vector<error_message> error_messages;
   extensions abi_extensions;
   may_not_exist<std::vector<variant_def>> variants;
   may_not_exist<std::vector<action_result_def>> action_results;
};

} // namespace forge::chain

export namespace forge::raw {

template <typename Stream, typename T>
void pack(Stream& stream, const forge::chain::may_not_exist<T>& value) {
   forge::raw::pack(stream, value.value);
}

template <typename Stream, typename T>
void unpack(Stream& stream, forge::chain::may_not_exist<T>& value) {
   if constexpr (requires { stream.remaining(); }) {
      if (stream.remaining()) {
         forge::raw::unpack(stream, value.value);
      } else {
         value.value = T{};
      }
   } else {
      forge::raw::unpack(stream, value.value);
   }
}

} // namespace forge::raw

export namespace forge::chain {
   BOOST_DESCRIBE_STRUCT(type_def, (), (new_type_name, type))
   BOOST_DESCRIBE_STRUCT(field_def, (), (name, type))
   BOOST_DESCRIBE_STRUCT(struct_def, (), (name, base, fields))
   BOOST_DESCRIBE_STRUCT(action_def, (), (name, type, ricardian_contract))
   BOOST_DESCRIBE_STRUCT(table_def, (), (name, index_type, key_names, key_types, type))
   BOOST_DESCRIBE_STRUCT(clause_pair, (), (id, body))
   BOOST_DESCRIBE_STRUCT(error_message, (), (error_code, error_msg))
   BOOST_DESCRIBE_STRUCT(variant_def, (), (name, types))
   BOOST_DESCRIBE_STRUCT(action_result_def, (), (name, result_type))
   BOOST_DESCRIBE_STRUCT(abi_def, (), (version, types, structs, actions, tables, ricardian_clauses, error_messages, abi_extensions, variants, action_results))

   inline void to_variant(const abi_def& value, forge::variant& variant) {
      forge::to_variant(value, variant);
   }

   inline void from_variant(const forge::variant& variant, abi_def& value) {
      forge::from_variant(variant, value);
   }
}

export namespace forge::raw {

template <typename Stream>
void pack(Stream& stream, const forge::chain::abi_def& value) {
   forge::raw::pack(stream, value.version);
   forge::raw::pack(stream, value.types);
   forge::raw::pack(stream, value.structs);
   forge::raw::pack(stream, value.actions);
   forge::raw::pack(stream, value.tables);
   forge::raw::pack(stream, value.ricardian_clauses);
   forge::raw::pack(stream, value.error_messages);
   forge::raw::pack(stream, value.abi_extensions);
   forge::raw::pack(stream, value.variants);
   forge::raw::pack(stream, value.action_results);
}

template <typename Stream>
void unpack(Stream& stream, forge::chain::abi_def& value) {
   forge::raw::unpack(stream, value.version);
   forge::raw::unpack(stream, value.types);
   forge::raw::unpack(stream, value.structs);
   forge::raw::unpack(stream, value.actions);
   forge::raw::unpack(stream, value.tables);
   forge::raw::unpack(stream, value.ricardian_clauses);
   forge::raw::unpack(stream, value.error_messages);
   forge::raw::unpack(stream, value.abi_extensions);
   forge::raw::unpack(stream, value.variants);
   forge::raw::unpack(stream, value.action_results);
}

inline forge::chain::bytes pack(const forge::chain::abi_def& value) {
   forge::datastream<std::size_t> size_stream;
   forge::raw::pack(size_stream, value);

   forge::chain::bytes out(size_stream.tellp());
   if (!out.empty()) {
      forge::datastream<std::uint8_t*> stream(out.data(), out.size());
      forge::raw::pack(stream, value);
   }
   return out;
}

} // namespace forge::raw

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::type_def)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::field_def)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::struct_def)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::action_def)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::table_def)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::clause_pair)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::error_message)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::variant_def)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::action_result_def)
