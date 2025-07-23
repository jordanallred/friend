#!/bin/bash

# C2 Framework Demo Script
# This script builds and launches all components of the C2 system

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
log() {
    echo -e "${BLUE}[DEMO]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Cleanup function
cleanup() {
    log "Shutting down all processes..."
    
    # Kill background processes
    if [[ -n $SERVER_PID ]]; then
        kill $SERVER_PID 2>/dev/null || true
        success "Command server stopped"
    fi
    
    if [[ -n $BEACON_PID ]]; then
        kill $BEACON_PID 2>/dev/null || true
        success "Beacon client stopped"
    fi
    
    # Kill any remaining processes
    pkill -f "uv run app.py" 2>/dev/null || true
    pkill -f "./build/beacon" 2>/dev/null || true
    
    log "Demo cleanup complete"
    exit 0
}

# Set trap for cleanup on script exit
trap cleanup EXIT INT TERM

log "Starting C2 Framework Demo"
log "========================================="

# Step 1: Build the beacon client
log "Building beacon client..."
cd beacon

# Create build directory if it doesn't exist
if [[ ! -d "build" ]]; then
    mkdir build
fi

# Build with cmake
cmake -B build
cmake --build build

if [[ ! -f "build/beacon" ]]; then
    error "Failed to build beacon client"
    exit 1
fi

success "Beacon client built successfully"
cd ..

# Step 2: Start the command server
log "Starting command server on localhost:8000..."
cd command
uv run app.py &
SERVER_PID=$!
cd ..

# Wait for server to start
sleep 3

# Check if server is running
if ! curl -s http://localhost:8000/status > /dev/null; then
    error "Command server failed to start"
    exit 1
fi

success "Command server started (PID: $SERVER_PID)"

# Step 3: Start the beacon client
log "Starting beacon client..."
cd beacon
./build/beacon &
BEACON_PID=$!
cd ..

# Wait for beacon to register
sleep 2

success "Beacon client started (PID: $BEACON_PID)"

# Step 4: Verify connection
log "Verifying beacon connection..."
CONNECTION_COUNT=$(curl -s http://localhost:8000/status | python3 -c "import sys, json; print(len(json.load(sys.stdin)))" 2>/dev/null || echo "0")

if [[ $CONNECTION_COUNT -gt 0 ]]; then
    success "Beacon successfully connected! ($CONNECTION_COUNT active connections)"
else
    warn "Beacon may not be connected yet (this is normal on first startup)"
fi

# Step 5: Display demo instructions
log "========================================="
log "Demo Environment Ready!"
log "========================================="
echo
log "Services running:"
echo "  • Command Server: http://localhost:8000"
echo "  • Beacon Client: Connected and polling for tasks"
echo
log "Starting Textual GUI..."
echo
log "Demo Instructions:"
echo "  1. The GUI will show your connected beacon"
echo "  2. Select the connection in the table"
echo "  3. Type commands like 'ls', 'whoami', 'pwd' in the input box"
echo "  4. Press Enter or click 'Send Command'"
echo "  5. Click on any task row to view full output"
echo "  6. Use 'r' to refresh, 'c' to clear logs, 'q' to quit"
echo
log "Press Ctrl+C in this terminal to stop all services"
echo

# Step 6: Start the GUI (this will block until user quits)
cd gui
python main.py

# The cleanup function will handle stopping all services when the GUI exits