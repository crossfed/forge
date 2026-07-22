import forge.crypto.math.bigint;

int main() {
   const auto value = forge::crypto::math::bigint{42};
   return value.to_int64() == 42 ? 0 : 1;
}
