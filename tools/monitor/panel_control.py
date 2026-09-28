#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Poll the board's button mailbox using the same console as frame capture."""
import re
import time
from dataclasses import dataclass

STATE = re.compile(r"^FGSTATE rev=(\d+) requested=([01]) ack=(\d+) pc=(idle|running|ok|error|fall)\r?$", re.M)


@dataclass(frozen=True)
class State:
    revision: int
    requested: bool


def parse(text):
    matches = list(STATE.finditer(text))
    if not matches or 'FGEND' not in text[matches[-1].end():]:
        raise RuntimeError('没有收到完整 FGSTATE；请使用带 fgctl 的面板控制固件')
    match = matches[-1]
    return State(int(match[1]), match[2] == '1')


class PanelControl:
    def __init__(self, console, once=False):
        self.console = console
        self.once = once
        self.consumed = None
        self.last = None

    def exchange(self, command):
        text = self.console.run_command(command, 5, ['FGEND', 'command not found'], tail=0.02)
        return parse(text)

    def query(self):
        state = self.exchange('fgctl query')
        if state != self.last:
            print(f"面板：{'开始监控' if state.requested else '停止监控'} (rev={state.revision})", flush=True)
            self.last = state
        return state

    def acknowledge(self, state, status):
        return self.exchange(f'fgctl ack {state.revision} {status}')

    def wait_start(self, earliest):
        while True:
            state = self.query()
            if not state.requested:
                self.acknowledge(state, 'idle')
            elif state.revision != self.consumed and time.monotonic() >= earliest:
                # If the button changed during the ack command, re-evaluate.
                confirmed = self.acknowledge(state, 'running')
                if confirmed == state:
                    return state
            time.sleep(0.5)

    def finish(self, state, success, fall=False):
        status = ('fall' if fall else 'ok') if success else 'error'
        current = self.acknowledge(state, status)
        if self.once:
            self.consumed = state.revision
        if not current.requested:
            self.acknowledge(current, 'idle')
            print('面板：当前帧已结束，监控已停止', flush=True)
