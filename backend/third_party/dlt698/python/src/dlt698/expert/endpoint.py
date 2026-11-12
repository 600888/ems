"""显式驱动的专家接口；provider/backend 只能在所属线程及时执行。"""

from __future__ import annotations

from collections.abc import Callable
from types import TracebackType
from typing import Self

from .. import _native as n


class Endpoint:
    """持有显式驱动 Engine 的上下文，回调由 poll 调用线程处理。

    可以监听或连接，完整 Session 配置与 AdvancedServiceOptions 明确传入。
    回调异常传播给调用者；关闭后不再分发完成事件。不同 Engine 的 ProxyRouter
    路由必须由同一业务线程持续驱动双方，禁止在 callback 同步等待响应。
    """

    def __init__(
        self,
        options: n.SessionOptions | None = None,
        objects: n.ObjectRegistry | None = None,
        advanced: n.AdvancedServiceOptions | None = None,
        *,
        on_event: Callable[[n.Completion], None] | None = None,
        transparent: n.TransBridge | None = None,
    ):
        self.engine = n.Engine(
            options or n.SessionOptions(),
            objects,
            1024,
            advanced or n.AdvancedServiceOptions(),
            16 * 1024 * 1024,
            transparent,
        )
        self._on_event = on_event
        self._closed = False

    def poll(self, budget: float = 0.001) -> list[n.Completion]:
        """推进原生 I/O 并处理完成；预算单位秒、毫秒精度，回调耗时不被中断。"""
        events = self.engine.poll(budget)
        for event in events:
            callback = self._on_event
            if callback is None or self._closed:
                break
            callback(event)
        return events

    def close(self) -> None:
        """禁用 callback 后取消 socket/计时器并自然排空 I/O。"""
        if not self._closed:
            self._on_event = None
            self._closed = True
            self.engine.close()

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
            exc.add_note(f"dlt698 endpoint close failed: {cleanup_error}")
