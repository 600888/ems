"""由调用线程分发拥有型事件，原生 I/O 线程不调用 Python。"""

from __future__ import annotations

from collections.abc import Callable
from types import TracebackType
from typing import Self

from .._native import Event, EventQueue


class Lifecycle:
    """上下文收尾与显式事件分发；回调异常直接交给调用者。"""

    def __init__(self, on_event: Callable[[Event], None] | None = None):
        self._on_event = on_event
        self._events = EventQueue() if on_event is not None else None

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        try:
            self.close()
        except Exception as cleanup_error:
            if exc is None:
                raise
            exc.add_note(f"dlt698 close failed: {cleanup_error}")

    def close(self) -> None:
        raise NotImplementedError

    def dispatch_events(self, count: int = 256) -> int:
        """在本调用线程分发最多 count 项；回调可关闭对象，关闭后不再分发。"""
        queue = self._events
        if queue is None or self._on_event is None:
            return 0
        dispatched = 0
        for event in queue.drain(count):
            callback = self._on_event
            if callback is None:
                break
            callback(event)
            dispatched += 1
        return dispatched

    @property
    def dropped_events(self) -> int:
        """队列因事件数或字节预算超限而丢弃的总数。"""
        return self._events.dropped if self._events is not None else 0

    def _disable_events(self) -> None:
        self._on_event = None
        if self._events is not None:
            self._events.close()
