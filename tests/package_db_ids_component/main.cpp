import forge.db.ids.object_id;

int main() {
   using account_id = forge::db::ids::typed_id<1, 2>;
   const auto account = account_id{42};
   return account.as_object_id().instance == 42 ? 0 : 1;
}
