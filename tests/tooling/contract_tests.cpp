#include <boost/test/unit_test.hpp>

import forge.tooling.abi.command;
import forge.tooling.attributes.registry;
import forge.tooling.manifest.command;
import forge.tooling.validation.command;

BOOST_AUTO_TEST_SUITE(contract_tooling)

BOOST_AUTO_TEST_CASE(component_entry_points_are_linkable) {
   forge::tooling::attributes::register_all();

   const auto* arguments = static_cast<const char* const*>(nullptr);
   BOOST_TEST(forge::tooling::abi::command::run(0, arguments) == 1);
   BOOST_TEST(forge::tooling::validation::command::run(0, arguments) == 1);
   BOOST_TEST(forge::tooling::manifest::command::run(0, arguments) == 1);
}

BOOST_AUTO_TEST_SUITE_END()
