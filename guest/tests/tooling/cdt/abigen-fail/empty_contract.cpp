import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

class [[eosio::contract("hello")]] hello : public contract {};
