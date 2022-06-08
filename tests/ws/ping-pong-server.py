#!/usr/bin/env python3
import asyncio
import random
import string
import websockets
import sys
import logging

logging.basicConfig(
    level=logging.DEBUG,
)

def make_random_string(length):
    return ''.join(random.choice(string.ascii_lowercase) for i in range(length))


async def main(ws, path):
    while True:
        data = await ws.recv()
        print(f'received: {data}')
        if data == 'ping':
            await ws.send('pong')



if __name__ == '__main__':
    start_server = websockets.serve(main, "localhost", 8765)
    asyncio.get_event_loop().run_until_complete(start_server)
    asyncio.get_event_loop().run_forever()
