"""Fixture subprocess ownership, terminal cleanup and raw output capture."""

from contextvars import ContextVar, Token
from pathlib import Path
import shutil
import signal
import subprocess
from typing import Optional


def tail_text(path: Path, limit: int = 20) -> str:
    if not path.exists():
        return "<missing log>"
    lines = path.read_text(errors="replace").splitlines()
    return "\n".join(lines[-limit:])


class Listener:
    def __init__(self, process: subprocess.Popen, ready: dict, stop_file: Optional[Path], log_file: Path, log_handle,
                 command: list[str]):
        self.process = process
        self.ready = ready
        self.stop_file = stop_file
        self.log_file = log_file
        self.log_handle = log_handle
        self.command = command
        # Keep a mutable terminal record so artifacts built after close include
        # the process outcome without inventing a separate listener result.
        self.terminal_status: dict[str, object] = {"exit_code": None, "termination": "running"}
        self.cleanup_errors: list[str] = []
        self.closed = False
        self.outputs: list[dict] = []

    def evidence(self) -> dict:
        record = {"pid": self.process.pid, "command": self.command, "log_file": str(self.log_file),
                  "terminal_status": self.terminal_status, "ready": self.ready}
        record["outputs"] = self.outputs
        return record

    def capture_outputs(self) -> None:
        # Snapshot before a retry can reset/overwrite the requested result path.
        # Partial JSON is raw output, not a fabricated validated result payload.
        for flag in ("--ready-file", "--result-file"):
            if flag in self.command:
                path = Path(self.command[self.command.index(flag) + 1])
                output = {"argument": flag, "path": str(path), "exists": path.is_file()}
                if path.is_file():
                    snapshot = self.log_file.with_name(f"{self.log_file.name}.{flag[2:]}.json")
                    shutil.copyfile(path, snapshot)
                    output["log_file"] = str(snapshot)
                self.outputs.append(output)

    def close(self) -> list[str]:
        if self.closed:
            return self.cleanup_errors
        self.closed = True
        def failure(message):
            self.cleanup_errors.append(f"pid={self.process.pid}; log={self.log_file}: {message}")

        try:
            exit_code = None
            try:
                exit_code = self.process.poll()
            except Exception as error:
                failure(f"process poll failed: {error}")
            if exit_code is None and self.stop_file is not None:
                try:
                    self.stop_file.write_text("stop\n")
                    exit_code = self.process.wait(timeout=5)
                except Exception as error:
                    failure(f"graceful stop failed: {error}")
            if exit_code is None:
                self.terminal_status["termination"] = "terminated"
                failure("forced SIGTERM (even if the process subsequently exits 0)")
                try:
                    self.process.send_signal(signal.SIGTERM)
                except Exception as error:
                    failure(f"SIGTERM failed: {error}")
                try:
                    exit_code = self.process.wait(timeout=5)
                except Exception as error:
                    failure(f"SIGTERM join failed: {error}")
                    self.terminal_status["termination"] = "killed"
                    failure("forced SIGKILL")
                    try:
                        self.process.kill()
                    except Exception as error:
                        failure(f"SIGKILL failed: {error}")
                    try:
                        exit_code = self.process.wait(timeout=5)
                    except Exception as error:
                        failure(f"SIGKILL join failed: {error}")
            else:
                self.terminal_status["termination"] = "graceful"
            self.terminal_status["exit_code"] = exit_code
            if exit_code != 0:
                failure(f"terminal exit code {exit_code}")
        except Exception as error:
            failure(f"process cleanup failed: {error}")
        finally:
            try:
                self.log_handle.close()
            except Exception as error:
                failure(f"log close failed: {error}")
            try:
                self.capture_outputs()
            except Exception as error:
                failure(f"raw output capture failed: {error}")
        return self.cleanup_errors


class ProcessScope:
    def __init__(self):
        self.processes: list[Listener] = []
        self.close_failures: dict[int, str] = {}
        self.failed_spawns: list[dict] = []
        self.attempts: list[dict] = []

    def close(self) -> list[str]:
        errors = []
        for process in reversed(self.processes):
            key = id(process)
            if key not in self.close_failures:
                try:
                    errors.extend(process.close())
                except Exception as error:
                    self.close_failures[key] = f"pid={process.process.pid}; log={process.log_file}: unexpected close failure: {error}"
            if key in self.close_failures:
                errors.extend(process.cleanup_errors)
                errors.append(self.close_failures[key])
        return errors

    def evidence(self) -> list[dict]:
        return [process.evidence() for process in self.processes] + self.failed_spawns


_process_scope: ContextVar[Optional[ProcessScope]] = ContextVar("interop_process_scope", default=None)


def current_scope() -> Optional[ProcessScope]:
    return _process_scope.get()


def enter_scope() -> tuple[ProcessScope, Token]:
    if current_scope() is not None:
        raise RuntimeError("fixture scenario already has an owner")
    scope = ProcessScope()
    return scope, _process_scope.set(scope)


def exit_scope(token: Token) -> None:
    _process_scope.reset(token)


def spawn_owned(command: list[str], log_file: Path, stop_file: Optional[Path] = None,
                attempt: Optional[dict] = None) -> Listener:
    scope = current_scope()
    if scope is None:
        raise RuntimeError("fixture process requires an owning scenario")
    log = None
    try:
        log = log_file.open("w")
        if attempt is not None:
            attempt["log_file"] = str(log_file)
        # Output freshness is independent of whether the peer store is reused.
        # Previous attempts have already captured immutable raw snapshots.
        for flag in ("--ready-file", "--result-file"):
            if flag in command:
                Path(command[command.index(flag) + 1]).unlink(missing_ok=True)
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    except BaseException as error:
        record = {"command": command, "requested_log_file": str(log_file), "spawn_error": str(error)}
        if log is not None:
            record["log_file"] = str(log_file)
            log.close()
        scope.failed_spawns.append(record)
        if attempt is not None:
            attempt["failure_class"] = "log_open_error" if log is None else "spawn_error"
            attempt["spawn_error"] = str(error)
            attempt["log_tail"] = "<log not opened>" if log is None else tail_text(log_file)
        raise
    owned = Listener(process, {}, stop_file, log_file, log, command)
    scope.processes.append(owned)
    if attempt is not None:
        attempt["pid"] = process.pid
        attempt["terminal_status"] = owned.terminal_status
        attempt["outputs"] = owned.outputs
    return owned
