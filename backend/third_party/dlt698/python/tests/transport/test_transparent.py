"""第七类 PROXY 的显式异步完成、拥有型命令及重复完成隔离。"""

import time

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import Engine, ObjectRegistry, TransBridge


def test_transparent_queue_delivery_and_duplicate_completion():
    bridge = TransBridge(max_pending=2, max_bytes=1024)
    server = Engine(objects=ObjectRegistry(), transparent=bridge)
    client = Engine()
    port = d.Oad(oi=0xF201, attribute=2)
    jobs = []

    def wait(token, respond=False):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            server.poll()
            for job in bridge.drain():
                jobs.append(job)
                if respond:
                    response = n.ProxyTransResponse(
                        port=job.request.port, result=job.request.command + b" response"
                    )
                    assert bridge.complete(job.token, response)
                    assert not bridge.complete(job.token, response)
            for event in client.poll():
                if event.token == token:
                    assert event.error is None
                    return event
        raise AssertionError("transparent request did not finish")

    try:
        tcp_port = server.listen("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        wait(client.connect_tcp("127.0.0.1", tcp_port, d.ConnectionProfile.local_preset))
        request = n.ProxyRequest(
            payload=n.ProxyTransRequest(port=port, response_timeout_seconds=1, command=b"command")
        )
        response = wait(client.exchange(request), True).message
        assert response.payload.result == b"command response"
        # 原生超时取消桥中的操作，远端副作用未知，迟到完成不能再次调用处理器。
        response = wait(client.exchange(request), False).message
        assert isinstance(response.payload.result, int) and response.payload.result != 0
        late = jobs[-1]
        assert not bridge.complete(
            late.token, n.ProxyTransResponse(port=late.request.port, result=b"late")
        )
        server.close()
        assert jobs[0].request.command == b"command"
        assert bridge.drain() == []
    finally:
        client.close()
        server.close()
        bridge.close()
