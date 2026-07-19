#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).parent))

from run_relocation import is_linux_sdk_runtime  # noqa: E402


class RuntimeBundlePolicyTests(unittest.TestCase):
    def test_llvm_cxx_runtimes_are_sdk_owned(self) -> None:
        self.assertTrue(is_linux_sdk_runtime("/usr/lib/x86_64-linux-gnu/libstdc++.so.6"))
        self.assertTrue(is_linux_sdk_runtime("/usr/lib/llvm-22/lib/libc++.so.1"))
        self.assertTrue(is_linux_sdk_runtime("/usr/lib/llvm-22/lib/libc++abi.so.1"))
        self.assertTrue(is_linux_sdk_runtime("/lib/x86_64-linux-gnu/libunwind.so.1"))
        self.assertTrue(is_linux_sdk_runtime("/usr/lib/llvm-22/lib/libLLVM.so.22.1"))
        self.assertTrue(is_linux_sdk_runtime("/usr/lib/llvm-22/lib/libclang-cpp.so.22.1"))
        self.assertTrue(is_linux_sdk_runtime("/usr/lib/llvm-22/lib/liblldWasm.so.22.1"))

    def test_baseline_system_runtimes_remain_external(self) -> None:
        self.assertFalse(is_linux_sdk_runtime("/usr/lib/x86_64-linux-gnu/libc.so.6"))


if __name__ == "__main__":
    unittest.main()
