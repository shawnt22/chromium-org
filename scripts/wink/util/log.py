from types import SimpleNamespace
from typing import Protocol
from datetime import datetime

class Log(Protocol):
    def d(*args) -> None: ...
    def i(*args) -> None: ...
    def w(*args) -> None: ...
    def e(*args) -> None: ...

def now():
    return datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]

log: Log = SimpleNamespace(
    d = lambda *args: print(now(), 'D', *args),
    i = lambda *args: print(now(), 'I', *args),
    w = lambda *args: print(now(), 'W', *args),
    e = lambda *args: print(now(), 'E', *args),
)