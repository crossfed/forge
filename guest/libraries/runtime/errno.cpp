#include "details/errno.hxx"

namespace forge::contract::runtime {
int errno_value = 0;
}

extern "C" int* __forge_contract_errno() {
   return &forge::contract::runtime::errno_value;
}
