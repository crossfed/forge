import forge.db.ids.object_id;

int main() {
   using account_id = forge::db::ids::typed_id<1, 2>;
   const auto account = account_id{42};
   return account.instance == 42 && forge::db::ids::typed_id_like<account_id> ? 0 : 1;
}
