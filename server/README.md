# BeagleLogic Web Server (Node.js + Socket.IO)

A web-based interface for BeagleLogic that provides real-time data acquisition and visualization through a browser. This server uses Node.js, Express, and Socket.IO to enable WebSocket-based streaming to web clients.

## Purpose

This server provides a **web-based alternative** to desktop logic analyzer applications by:
- Serving a web application for browser-based data visualization
- Streaming BeagleLogic data via WebSocket (Socket.IO)
- Integrating with sigrok-cli for data capture and processing
- Enabling remote access from any device with a web browser

## Features

- **Browser-Based**: No desktop software installation required
- **Real-Time Streaming**: WebSocket (Socket.IO) for low-latency data transfer
- **Cross-Platform Clients**: Works on any device (Windows, Mac, Linux, tablets, phones)
- **Remote Access**: Access your BeagleBone from anywhere on the network
- **Integration**: Uses sigrok-cli for data acquisition

## Installation

Recommended install using "beaglelogic_setup.sh" script at beaglelogic root directory

The server will start on **port 4000** (default).

### Access the Web Interface

Open a web browser and navigate to:

```
http://192.168.7.2:4000/      # USB connection
http://beaglebone.local:4000/  # mDNS (if configured)
http://<ip-address>:4000/      # Network connection
```

## Architecture

```
Web Browser (Client)
    ↓ HTTP + WebSocket (Socket.IO)
Node.js Server (Port 4000)
    ↓ Spawns process
sigrok-cli
    ↓ Reads from
/dev/beaglelogic (Kernel Module)
    ↓ DMA from
PRU Firmware → Hardware
```

## Socket.IO Events

### Client → Server Events

#### `sigrok-test`
Request data acquisition using sigrok-cli.

**Parameters:**
```javascript
{
  samplecount: "1000",      // Number of samples
  samplerate: "100000000",  // Sample rate (Hz)
  channels: "0-7",          // Channel selection
  extended: false,          // Extended mode (14 channels)
  trigger: null             // Optional trigger pattern
}
```

**Example Trigger Patterns:**
```javascript
// Trigger on channel 0 rising edge
trigger: "0=r"

// Trigger on specific bit pattern
trigger: "0=1,1=0,2=1"

// Multiple conditions
trigger: "0=r,1=1"
```

#### `beaglelogic-test`
Request raw data from BeagleLogic device.

**Parameters:**
```javascript
{
  buffersize: 1048576,  // Buffer size in bytes
  samplerate: 10000000  // Sample rate in Hz
}
```

### Server → Client Events

#### `sigrok-data`
Sends captured data as ASCII format.

**Data Format:**
```
0 1 0 1 1 0 0 1
1 1 0 1 1 0 0 1
0 0 1 1 1 0 1 0
...
```

#### `sigrok-data-header`
Metadata about incoming binary data.

```javascript
{
  size: 1048576,    // Data size in bytes
  enc: 'base64'     // Encoding format
}
```

#### `sigrok-data-big`
Chunks of base64-encoded binary data (512 KB chunks).

**Use Case:** For large captures that need to be transferred efficiently.

## Server API Details

### HTTP Endpoints

- **`GET /`** - Serves the static web application
- Static files are served from `../webapp/` directory

### WebSocket Protocol

The server uses Socket.IO with:
- **Ping Interval**: 60 seconds
- **Ping Timeout**: 120 seconds
- Long-lived connections for real-time streaming

### Data Compression

The server includes LZ4 compression support for efficient data transfer (currently used for large binary transfers).

**Compression Ratios:**
- Typical logic captures: 3:1 to 10:1
- Idle bus (repetitive patterns): 50:1 or better
- Active random data: ~2:1

## Troubleshooting

### Server won't start

```bash
# Check if port is in use
sudo netstat -tlnp | grep 4000

# Check Node.js installed
node --version

# Install dependencies
cd server/
npm install
```

### Can't access from browser

```bash
# Check server is running
ps aux | grep node

# Check firewall
sudo ufw allow 4000/tcp

# Test locally
curl http://localhost:4000
```

### sigrok-cli errors

```bash
# Verify sigrok-cli installed
which sigrok-cli
sigrok-cli --version

# Test sigrok-cli manually
sigrok-cli -d beaglelogic --samples 100 -c samplerate=1000000

# Check device node
ls -l /dev/beaglelogic

# Check kernel module
lsmod | grep beaglelogic
```

### WebSocket connection fails

```bash
# Check Socket.IO version compatibility
npm list socket.io

# Enable CORS if needed (edit app.js)
# Add to socket.io initialization:
# const io = require('socket.io')(server, {
#   cors: {
#     origin: "*",
#     methods: ["GET", "POST"]
#   }
# });
```

### Data transfer issues

```bash
# Monitor server logs
node app.js | tee server.log

# Check network bandwidth
iperf3 -s  # On BeagleBone
iperf3 -c <beaglebone-ip>  # On client

# Reduce sample rate for slow connections
# In client: samplerate: "1000000" (1 MHz instead of 100 MHz)
```

## Performance

- **Memory Usage**: ~30-50 MB (Node.js runtime + application)
- **CPU Usage**: ~5-10% idle, 20-40% during streaming
- **Latency**: ~50-100ms for WebSocket communication
- **Concurrent Connections**: Currently limited to 1 client
- **Max Data Rate**: ~10-20 MB/s (limited by WebSocket overhead)

**Optimization Tips:**
1. Use binary data mode instead of ASCII for large captures
2. Enable LZ4 compression for repetitive signals
3. Reduce sample rate to minimum required for signal analysis
4. Use trigger to capture only relevant events

## Security Considerations

**Warning**: This server has no authentication by default.

## Future Enhancements

Potential improvements for this server:

### UI Framework
- Replace static webapp with React/Vue.js for dynamic interface
- Add component library (Material-UI, Bootstrap)
- Implement state management (Redux, Vuex)

### Visualization (fun ideas)
- Real-time plotting with Plotly.js or Chart.js
- Waveform display canvas
- Protocol decoder visualization (UART, SPI, I2C)
- Timing diagram generation
- Bus activity heatmaps

### Features
- **Authentication**: User login system with role-based access
- **REST API**: RESTful endpoints for configuration and status
- **Multi-Client**: Support multiple concurrent connections with resource management
- **WebRTC**: Use WebRTC for even lower latency streaming
- **File Management**: Download/upload capture files
- **Trigger Editor**: Visual trigger pattern editor
- **Sample Rate Calculator**: Helper to determine optimal sample rate for protocols

### Data Processing
- In-browser protocol decoding (UART, SPI, I2C parsing)
- Measurement tools (frequency, duty cycle, pulse width)
- Export to VCD, CSV, or other formats
- Capture session management (save/load/replay)

## Related Components
- **webapp/** - Static web application files (served by this server)

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | 4000 | HTTP server port |
| `NODE_ENV` | development | Environment (development/production) |
| `LOG_LEVEL` | info | Logging verbosity |

## License

MIT License

## Author

Kumar Abhishek (original BeagleLogic project)

## Links

- [BeagleLogic Documentation](../docs/index.md)
- [Socket.IO Documentation](https://socket.io/docs/)
- [Express.js Documentation](https://expressjs.com/)
- [sigrok Project](https://sigrok.org/)
