export module forge.raw.varint;

export import forge.raw.varint_value;

import forge.variant.value;

export namespace forge {

void to_variant(const signed_int& value, variant& out);
void from_variant(const variant& value, signed_int& out);
void to_variant(const unsigned_int& value, variant& out);
void from_variant(const variant& value, unsigned_int& out);

} // namespace forge
