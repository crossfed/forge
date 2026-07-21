import forge.codec.base64;

int main() {
   return forge::codec::base64::encode("forge") == "Zm9yZ2U=" ? 0 : 1;
}
