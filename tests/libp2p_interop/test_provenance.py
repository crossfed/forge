#!/usr/bin/env python3
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

from provenance import worktree_identity


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def initialize_repository(root: Path) -> None:
    root.mkdir()
    git(root, "init")
    git(root, "config", "user.name", "Forge provenance test")
    git(root, "config", "user.email", "forge-provenance@example.invalid")


def commit(root: Path, message: str) -> str:
    git(root, "add", ".")
    git(root, "commit", "-m", message)
    return git(root, "rev-parse", "HEAD")


def add_submodule(root: Path, source: Path, destination: str) -> None:
    git(root, "-c", "protocol.file.allow=always", "submodule", "add", str(source), destination)


class WorktreeFingerprintTest(unittest.TestCase):
    def make_nested_worktree(self, temporary: Path) -> tuple[Path, str, str, str]:
        leaf = temporary / "leaf"
        initialize_repository(leaf)
        (leaf / "leaf.txt").write_text("first\n")
        leaf_first = commit(leaf, "leaf first")
        (leaf / "leaf.txt").write_text("second\n")
        leaf_second = commit(leaf, "leaf second")
        git(leaf, "checkout", leaf_first)

        module = temporary / "module"
        initialize_repository(module)
        add_submodule(module, leaf, "nested")
        (module / "module.txt").write_text("first\n")
        module_first = commit(module, "module first")
        (module / "module.txt").write_text("second\n")
        module_second = commit(module, "module second")
        git(module, "checkout", module_first)

        root = temporary / "root"
        initialize_repository(root)
        add_submodule(root, module, "module")
        commit(root, "root")
        git(root, "-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive")
        return root, module_first, module_second, leaf_second

    def restore_clean_submodules(self, root: Path) -> None:
        git(root, "-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive", "--force")
        git(root / "module", "clean", "-fd")
        git(root / "module" / "nested", "clean", "-fd")

    def test_fingerprint_tracks_direct_and_nested_submodule_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root, module_first, module_second, leaf_second = self.make_nested_worktree(Path(directory))
            clean = worktree_identity(root)
            self.assertFalse(clean.dirty)

            git(root / "module", "checkout", module_second)
            different_checkout = worktree_identity(root)
            self.assertNotEqual(different_checkout.fingerprint, clean.fingerprint)
            self.assertTrue(different_checkout.dirty)
            self.restore_clean_submodules(root)
            self.assertEqual(worktree_identity(root), clean)
            self.assertEqual(git(root / "module", "rev-parse", "HEAD"), module_first)

            (root / "module" / "module.txt").write_text("dirty\n")
            dirty_tracked = worktree_identity(root)
            self.assertNotEqual(dirty_tracked.fingerprint, clean.fingerprint)
            self.assertTrue(dirty_tracked.dirty)
            self.restore_clean_submodules(root)
            self.assertEqual(worktree_identity(root), clean)

            module_file = root / "module" / "module.txt"
            module_file.write_text("dirty before chmod\n")
            dirty_before_chmod = worktree_identity(root)
            module_file.chmod(module_file.stat().st_mode ^ stat.S_IXUSR)
            dirty_after_chmod = worktree_identity(root)
            self.assertTrue(dirty_before_chmod.dirty)
            self.assertTrue(dirty_after_chmod.dirty)
            self.assertNotEqual(dirty_after_chmod.fingerprint, dirty_before_chmod.fingerprint)
            self.restore_clean_submodules(root)
            self.assertEqual(worktree_identity(root), clean)

            (root / "module" / "untracked.txt").write_text("untracked\n")
            dirty_untracked = worktree_identity(root)
            self.assertNotEqual(dirty_untracked.fingerprint, clean.fingerprint)
            self.assertTrue(dirty_untracked.dirty)
            self.restore_clean_submodules(root)
            self.assertEqual(worktree_identity(root), clean)

            git(root / "module" / "nested", "checkout", leaf_second)
            dirty_nested = worktree_identity(root)
            self.assertNotEqual(dirty_nested.fingerprint, clean.fingerprint)
            self.assertTrue(dirty_nested.dirty)
            self.restore_clean_submodules(root)
            self.assertEqual(worktree_identity(root), clean)


if __name__ == "__main__":
    unittest.main()
