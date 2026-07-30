#if !defined(NDEBUG) && !defined(FORGE_CONTRACT_CHECKED_CONFIGURATION)
#error "host configuration was not forwarded to guest targets"
#endif

void forge_contract_release_configuration_probe() {}
