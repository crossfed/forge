#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

import forge.contract.abi.command;
import forge.contract.attributes.registry;
import forge.contract.graph;
import forge.contract.manifest.command;
import forge.contract.validation.command;

namespace {

std::string json_quote(std::string value) {
   auto result = std::string{"\""};
   for (const auto character : value) {
      if (character == '\\' || character == '"') {
         result.push_back('\\');
      }
      result.push_back(character);
   }
   result.push_back('"');
   return result;
}

class graph_fixture {
 public:
   graph_fixture() {
      const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
      root_ = std::filesystem::temp_directory_path() / ("forge-contract-graph-" + std::to_string(nonce));
      std::filesystem::create_directories(root_);
      write(root_ / "contract.cpp", "void contract_source() {}\n");
      write(root_ / "protocol.cppm", "export module product.protocol;\n");
   }

   ~graph_fixture() {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
   }

   std::filesystem::path graph(std::string libraries, std::string components,
                               std::string root_libraries = R"(["product.protocol"])",
                               std::string root_components = "[]") const {
      const auto path = root_ / "contract-graph.json";
      write(path, R"({"schema":2,"root":{"owner":"contract:test","source_root":)" + json_quote(root_.string()) +
                      R"(,"files":[{"logical_path":"contract.cpp","physical_path":)" +
                      json_quote((root_ / "contract.cpp").string()) + R"(,"role":"dispatch_source"}],"libraries":)" +
                      std::move(root_libraries) + R"(,"components":)" + std::move(root_components) +
                      R"(},"libraries":)" + std::move(libraries) + R"(,"components":)" + std::move(components) + "}");
      return path;
   }

   std::string library(std::string dependencies = "[]") const {
      return R"([{"id":"product.protocol","source_roots":[)" + json_quote(root_.string()) + R"(],"module_bases":[)" +
             json_quote(root_.string()) + R"(],"files":[{"logical_path":"protocol.cppm","physical_path":)" +
             json_quote((root_ / "protocol.cppm").string()) + R"(,"role":"module"}],"dependencies":)" +
             std::move(dependencies) + "}]";
   }

   std::filesystem::path document(std::string value) const {
      const auto path = root_ / "custom-contract-graph.json";
      write(path, value);
      return path;
   }

   std::filesystem::path missing_document() const {
      return root_ / "missing-contract-graph.json";
   }

 private:
   static void write(const std::filesystem::path& path, const std::string& value) {
      auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
      output << value;
      if (!output) {
         throw std::runtime_error{"failed to write contract graph fixture"};
      }
   }

   std::filesystem::path root_;
};

bool contains_message(const std::runtime_error& error, const std::string& expected) {
   return std::string{error.what()}.contains(expected);
}

bool contains_context(const forge::exceptions::base& error, const std::string& key, const std::string& expected) {
   return std::ranges::any_of(error.context(), [&](const auto& field) {
      return field.key == key && field.value.contains(expected);
   });
}

void read_graph(const std::filesystem::path& path) {
   const auto descriptor = forge::contract::graph::read(path);
   static_cast<void>(descriptor);
}

} // namespace

BOOST_AUTO_TEST_SUITE(contract_tooling)

BOOST_AUTO_TEST_CASE(component_entry_points_are_linkable) {
   forge::contract::attributes::register_all();

   const auto* arguments = static_cast<const char* const*>(nullptr);
   BOOST_TEST(forge::contract::abi::command::run(0, arguments) == 1);
   BOOST_TEST(forge::contract::validation::command::run(0, arguments) == 1);
   BOOST_TEST(forge::contract::manifest::command::run(0, arguments) == 1);
}

BOOST_AUTO_TEST_CASE(contract_graph_accepts_explicit_library_and_component_edges) {
   const auto fixture = graph_fixture{};
   const auto graph = fixture.graph(fixture.library(R"([{"kind":"component","id":"forge.raw","scope":"public"}])"),
                                    R"([{"id":"forge.raw","modules":["forge.raw.codec"],"dependencies":[]}])");
   const auto descriptor = forge::contract::graph::read(graph);

   BOOST_TEST(descriptor.root_owner == "contract:test");
   BOOST_TEST(descriptor.files.size() == 2U);
   BOOST_TEST(descriptor.dependencies.size() == 2U);
   BOOST_TEST(descriptor.components.size() == 1U);
}

BOOST_AUTO_TEST_CASE(contract_graph_reports_typed_read_failures) {
   const auto fixture = graph_fixture{};
   BOOST_CHECK_EXCEPTION(
       read_graph(fixture.missing_document()), forge::contract::graph::exceptions::read_error,
       [](const auto& error) { return contains_context(error, "path", "missing-contract-graph.json"); });
}

BOOST_AUTO_TEST_CASE(contract_graph_reports_typed_descriptor_failures) {
   const auto fixture = graph_fixture{};
   BOOST_CHECK_EXCEPTION(
       read_graph(fixture.document("{not-json")), forge::contract::graph::exceptions::invalid_descriptor,
       [](const auto& error) {
          return contains_context(error, "path", "custom-contract-graph.json") &&
                 contains_context(error, "cause", "valid JSON object");
       });
}

BOOST_AUTO_TEST_CASE(contract_graph_rejects_missing_component_ids) {
   const auto fixture = graph_fixture{};
   BOOST_CHECK_EXCEPTION(read_graph(fixture.graph(
                             fixture.library(R"([{"kind":"component","id":"forge.missing","scope":"public"}])"), "[]")),
                         std::runtime_error,
                         [](const auto& error) { return contains_message(error, "unknown component: forge.missing"); });
}

BOOST_AUTO_TEST_CASE(contract_graph_rejects_ids_shared_by_libraries_and_components) {
   const auto fixture = graph_fixture{};
   BOOST_CHECK_EXCEPTION(
       read_graph(fixture.graph(fixture.library(),
                                R"([{"id":"product.protocol","modules":["product.component"],"dependencies":[]}])")),
       std::runtime_error,
       [](const auto& error) { return contains_message(error, "shared by a library and component"); });
}

BOOST_AUTO_TEST_CASE(contract_graph_rejects_duplicate_module_ownership) {
   const auto fixture = graph_fixture{};
   BOOST_CHECK_EXCEPTION(read_graph(fixture.graph("[]",
                                                  R"([{"id":"forge.one","modules":["forge.shared"],"dependencies":[]},)"
                                                  R"({"id":"forge.two","modules":["forge.shared"],"dependencies":[]}])",
                                                  "[]", R"(["forge.one"])")),
                         std::runtime_error,
                         [](const auto& error) { return contains_message(error, "invalid or duplicate module"); });
}

BOOST_AUTO_TEST_CASE(contract_graph_rejects_component_cycles) {
   const auto fixture = graph_fixture{};
   BOOST_CHECK_EXCEPTION(
       read_graph(fixture.graph("[]",
                                R"([{"id":"forge.one","modules":["forge.one"],"dependencies":["forge.two"]},)"
                                R"({"id":"forge.two","modules":["forge.two"],"dependencies":["forge.one"]}])",
                                "[]", R"(["forge.one"])")),
       std::runtime_error, [](const auto& error) { return contains_message(error, "component dependency cycle"); });
}

BOOST_AUTO_TEST_SUITE_END()
