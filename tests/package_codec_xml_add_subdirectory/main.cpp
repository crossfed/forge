import forge.codec.xml;

int main() {
   auto doc = forge::codec::xml::document{.root = forge::codec::xml::element{.name = "Root"}};
   auto written = forge::codec::xml::write_value(doc);
   return written.ok() ? 0 : 1;
}
