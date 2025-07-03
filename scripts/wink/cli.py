from wink.util.log import log
import asyncio
import threading

async def say_hello():
    print("Hello")
    await asyncio.sleep(1)
    print("World")
    # raise Exception("This is an error")
    return 'abc'

async def main():
    # result = asyncio.run(say_hello())
    # log.d("say_hello completed with result:", result)
    asyncio.create_task(say_hello())  # Start the coroutine without waiting for it to finish
    log.i("Wink CLI is running")
    await asyncio.Event().wait()

if __name__ == "__main__":
    asyncio.run(main())
    threading.Event().wait()