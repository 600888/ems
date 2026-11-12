"""有期限子进程验证同步等待释放 GIL，另一线程可取消。"""

import subprocess
import sys


def test_blocking_get_releases_gil_and_can_be_cancelled():
    script = r"""
import socket
import threading
import dlt698 as d
listener=socket.socket()
listener.bind(('127.0.0.1',0)); listener.listen()
received=threading.Event(); stop=threading.Event()
def peer():
    conn,_=listener.accept()
    with conn:
        assert conn.recv(65536)
        received.set(); stop.wait(3)
peer_thread=threading.Thread(target=peer); peer_thread.start()
client=d.Client(d.ClientOptions(protocol=d.SessionOptions(request_timeout=30)))
client.connect_tcp('127.0.0.1',listener.getsockname()[1],d.ConnectionProfile.local_preset)
errors=[]
def read():
    try: client.get(d.Oad(oi=0x200f,attribute=2))
    except d.Dlt698Error as error: errors.append(error)
thread=threading.Thread(target=read); thread.start()
assert received.wait(2), 'GIL prevented peer/control thread from running'
client.request_disconnect(); thread.join(2)
assert not thread.is_alive(), 'native cancellation did not unblock GET'
assert errors and errors[0].code in (d.ErrorCode.cancelled,d.ErrorCode.closed)
client.close(); stop.set(); peer_thread.join(2); listener.close()
assert not peer_thread.is_alive()
"""
    subprocess.run([sys.executable, "-c", script], check=True, timeout=10)
