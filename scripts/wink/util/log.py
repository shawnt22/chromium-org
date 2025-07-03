from types import SimpleNamespace
from typing import Protocol
from datetime import datetime

class _Log(Protocol):
    def d(*args) -> None: ...
    def i(*args) -> None: ...
    def w(*args) -> None: ...
    def e(*args) -> None: ...

def _now():
    return datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]

log: _Log = SimpleNamespace(
    d = lambda *args: print(_now(), 'D', *args),
    i = lambda *args: print(_now(), 'I', *args),
    w = lambda *args: print(_now(), 'W', *args),
    e = lambda *args: print(_now(), 'E', *args),
)