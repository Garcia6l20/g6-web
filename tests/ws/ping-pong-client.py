#!/usr/bin/env python3
import asyncio
import random
import string
from websockets import client
import sys


def make_random_string(length):
    return ''.join(random.choice(string.ascii_lowercase) for i in range(length))


async def main():
    def gen():
        yield 'pi'
        yield 'ng'
    async with client.connect(f"ws://localhost:{sys.argv[1]}/") as ws:
        await ws.send(gen())
        print("Sent")
        #input('press ENTER')
        print("Receiving...")
        result = await ws.recv()
        print(f"Received {result}")
        assert result == "pong"
        # result = await ws.recv()
        # print(f"Received {result}")
        # assert result == "ping"
        # await ws.send("pong")


if __name__ == '__main__':
    asyncio.get_event_loop().run_until_complete(main())
