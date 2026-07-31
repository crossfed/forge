#include <boost/test/unit_test.hpp>

import forge.contract.abi.command;
import forge.contract.attributes.registry;
import forge.contract.manifest.command;
import forge.contract.validation.command;

BOOST_AUTO_TEST_SUITE(contract_tooling)

BOOST_AUTO_TEST_CASE(component_entry_points_are_linkable) {
   forge::contract::attributes::register_all();

   const auto* arguments = static_cast<const char* const*>(nullptr);
   BOOST_TEST(forge::contract::abi::command::run(0, arguments) == 1);
   BOOST_TEST(forge::contract::validation::command::run(0, arguments) == 1);
   BOOST_TEST(forge::contract::manifest::command::run(0, arguments) == 1);
}

BOOST_AUTO_TEST_SUITE_END()
