#include <eosio/datastream.hpp>

#include <cstdint>

struct eosio_datastream_record {
   std::uint32_t value = 0;
};

void compile_eosio_datastream_adl() {
   char bytes[16]{};
   auto stream = eosio::datastream<char*>{bytes, sizeof(bytes)};
   auto input = eosio_datastream_record{42};
   stream << input;

   stream.seekp(0);
   auto output = eosio_datastream_record{};
   stream >> output;
}
