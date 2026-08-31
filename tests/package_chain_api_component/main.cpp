import package.chain_api_component.read_e2e;
import package.chain_api_component.surface_checks;
import package.chain_api_component.verifier_fixture;
import package.chain_api_component.write_e2e;

bool portable_verified_client_package_contract();

int main() {
   package_chain_api_component::check_read_api_surface();
   package_chain_api_component::check_write_api_surface();
   package_chain_api_component::check_state_surface();
   package_chain_api_component::check_block_surface();
   package_chain_api_component::check_info_admin_surface();
   package_chain_api_component::check_codec_surface();
   package_chain_api_component::run_verifier_component_checks();
   package_chain_api_component::run_read_e2e();
   package_chain_api_component::run_write_e2e();
   return portable_verified_client_package_contract() ? 0 : 1;
}
