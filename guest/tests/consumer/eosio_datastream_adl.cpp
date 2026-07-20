#include <eosio/datastream.hpp>

#include <cstdint>

struct eosio_datastream_record {
   std::uint32_t value = 0;
};

struct eosio_datastream_array_record {
   std::uint32_t values[2]{};
};

class eosio_datastream_unsupported {
 public:
   eosio_datastream_unsupported() {}
};

template <typename T>
concept eosio_stream_writable = requires(eosio::datastream<char*>& stream, const T& value) { stream << value; };

static_assert(eosio_stream_writable<eosio_datastream_record>);
static_assert(eosio_stream_writable<eosio_datastream_array_record>);
static_assert(!eosio_stream_writable<eosio_datastream_unsupported>);

void compile_eosio_datastream_adl() {
   char bytes[32]{};
   auto stream = eosio::datastream<char*>{bytes, sizeof(bytes)};
   auto input = eosio_datastream_record{42};
   stream << input;

   auto array_input = eosio_datastream_array_record{{7, 9}};
   stream << array_input;

   stream.seekp(0);
   auto output = eosio_datastream_record{};
   stream >> output;

   auto array_output = eosio_datastream_array_record{};
   stream >> array_output;
}
