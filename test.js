const WebSocket = require('ws');
const TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE3MjMzNjQ4NjgsImlzcyI6IkFQSVRUVXpMY2hhN1JMeiIsIm5iZiI6MTcyMzM2MTI2OCwic3ViIjoiaWRlbnRpdHkiLCJ2aWRlbyI6eyJyb29tIjoibXktcm9vbSIsInJvb21Kb2luIjp0cnVlfX0.g8be8v3ktnKqOyy9cmGnZCNG2rzf88UvjwDP5dTOcgY"
// APITTUzLcha7RLz
// 3Z0d5IY5LCfPdggq3SNxFDe627rpyFnxyVPpY2EHZv5
// Create a WebSocket client that connects to the server
const ws = new WebSocket('wss://embeded-app-4da61xdw.livekit.cloud/rtc',{headers:{
    "authorization" :`Bearer ${TOKEN}`
}});

ws.on('open', () => {
    console.log('Connected to the server');
});

ws.on('message', (message) => {
    console.log(`Received: ${message}`);
});

ws.on('close', () => {
    console.log('Disconnected from the server');
});
