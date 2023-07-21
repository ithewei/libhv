#!/usr/bin/env python3

# pip3 install websockets

import asyncio
import websockets

async def echo(websocket):
    async for message in websocket:
        print(message)
        await websocket.send(message)

async def serve(port):
    async with websockets.serve(echo, "0.0.0.0", port):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(serve(9999))
