#!/bin/bash
cd "$(dirname "$0")"

PORT=5660

# Kill any existing test servers
pkill -f "test_server.py" 2>/dev/null || true
sleep 1

# Start server in background
echo "=== 9P Filesystem Integration Test ==="
echo ""
echo "Starting 9P test server on port $PORT..."
python3 -u test_server.py $PORT &
SERVER_PID=$!
sleep 2

echo ""
echo "Running client test..."
echo ""

# Run client
./test_9p_client localhost $PORT
CLIENT_EXIT=$?

echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

if [ $CLIENT_EXIT -eq 0 ]; then
    echo ""
    echo "=== All tests passed! ==="
    exit 0
else
    echo ""
    echo "=== Tests failed (exit code: $CLIENT_EXIT) ==="
    exit 1
fi
