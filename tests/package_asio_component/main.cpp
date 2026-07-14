import forge.asio.blocking;
import forge.asio.compute;
import forge.asio.runtime;
import forge.asio.task;

int main() {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = forge::asio::task::scheduler{runtime};
   auto compute = forge::asio::compute::pool{forge::asio::compute::pool::options{.worker_threads = 1}};

   const auto result = forge::asio::blocking::run(
       runtime, compute.get_executor().execute({.name = "package-smoke"}, [] { return 42; }));
   forge::asio::blocking::run(runtime, compute.shutdown());
   scheduler.stop();
   return result == 42 ? 0 : 1;
}
