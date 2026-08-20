#pragma once

namespace forge::contract::testing {

class compiler_builtins {
 public:
   template <typename T> using output = forge::vm::wasm::interpret::argument_proxy<T*, alignof(T)>;
   using int128_output = output<__int128>;
   using uint128_output = output<unsigned __int128>;
   using float128_output = output<float128>;

   void __ashlti3(int128_output result, std::uint64_t low, std::uint64_t high, std::uint32_t shift) const;
   void __ashrti3(int128_output result, std::uint64_t low, std::uint64_t high, std::uint32_t shift) const;
   void __lshlti3(int128_output result, std::uint64_t low, std::uint64_t high, std::uint32_t shift) const;
   void __lshrti3(int128_output result, std::uint64_t low, std::uint64_t high, std::uint32_t shift) const;
   void __divti3(int128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __udivti3(uint128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                  std::uint64_t high_b) const;
   void __multi3(uint128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __modti3(int128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __umodti3(uint128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                  std::uint64_t high_b) const;

   void __addtf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __subtf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __multf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __divtf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                 std::uint64_t high_b) const;
   void __negtf2(float128_output result, std::uint64_t low, std::uint64_t high) const;
   void __extendsftf2(float128_output result, float value) const;
   void __extenddftf2(float128_output result, double value) const;
   [[nodiscard]] double __trunctfdf2(std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] float __trunctfsf2(std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] std::int32_t __fixtfsi(std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] std::int64_t __fixtfdi(std::uint64_t low, std::uint64_t high) const;
   void __fixtfti(int128_output result, std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] std::uint32_t __fixunstfsi(std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] std::uint64_t __fixunstfdi(std::uint64_t low, std::uint64_t high) const;
   void __fixunstfti(uint128_output result, std::uint64_t low, std::uint64_t high) const;
   void __fixsfti(int128_output result, float value) const;
   void __fixdfti(int128_output result, double value) const;
   void __fixunssfti(uint128_output result, float value) const;
   void __fixunsdfti(uint128_output result, double value) const;
   [[nodiscard]] double __floatsidf(std::int32_t value) const;
   void __floatsitf(float128_output result, std::int32_t value) const;
   void __floatditf(float128_output result, std::uint64_t value) const;
   void __floatunsitf(float128_output result, std::uint32_t value) const;
   void __floatunditf(float128_output result, std::uint64_t value) const;
   [[nodiscard]] double __floattidf(std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] double __floatuntidf(std::uint64_t low, std::uint64_t high) const;
   [[nodiscard]] std::int32_t __cmptf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                       std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __eqtf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                      std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __netf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                      std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __getf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                      std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __gttf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                      std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __letf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                      std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __lttf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                      std::uint64_t high_b) const;
   [[nodiscard]] std::int32_t __unordtf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                         std::uint64_t high_b) const;
};

} // namespace forge::contract::testing
