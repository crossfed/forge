import forge.codec.hex;

int main() {
   return forge::codec::hex::encode(0x12abU, 8U) == "000012ab" ? 0 : 1;
}
