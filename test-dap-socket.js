#!/usr/bin/env node

/**
 * Simple test script to verify DAP socket communication
 */

const net = require('net');
const { spawn } = require('child_process');
const path = require('path');

const SOCKET_PATH = '/tmp/tads-dap-test.sock';
const FROBD_PATH = path.join(__dirname, 'build', 'frobd');
const GAME_PATH = path.join(__dirname, 'build', 'game.t3');

// Start frobd in the background
console.log('Starting frobd with socket:', SOCKET_PATH);
const frobd = spawn(FROBD_PATH, [
  '-i', 'plain',
  '-D', 'dap',
  '--dap-socket', SOCKET_PATH,
  GAME_PATH
]);

frobd.stderr.on('data', (data) => {
  console.log('[frobd]', data.toString().trim());
});

frobd.on('error', (err) => {
  console.error('Failed to start frobd:', err);
  process.exit(1);
});

frobd.on('exit', (code) => {
  console.log('frobd exited with code:', code);
});

// Wait for socket to be created
setTimeout(() => {
  console.log('Connecting to socket...');
  
  const client = net.createConnection(SOCKET_PATH);
  
  client.on('connect', () => {
    console.log('Connected to frobd via socket!');
    
    // Send a simple DAP initialize request
    const initRequest = {
      jsonrpc: '2.0',
      id: 1,
      method: 'initialize',
      params: {
        clientID: 'test',
        clientName: 'Test Client',
        adapterID: 'tads3',
        locale: 'en-US',
        linesStartAt1: true,
        columnsStartAt1: true,
        pathFormat: 'path'
      }
    };
    
    const message = JSON.stringify(initRequest);
    const header = `Content-Length: ${Buffer.byteLength(message)}\r\n\r\n`;
    
    console.log('Sending initialize request...');
    client.write(header + message);
  });
  
  let buffer = '';
  
  client.on('data', (data) => {
    buffer += data.toString();
    console.log('[Response]', buffer);
    
    // Give it a moment to process, then disconnect
    setTimeout(() => {
      console.log('Test successful! Closing connection...');
      client.end();
      frobd.kill();
      process.exit(0);
    }, 1000);
  });
  
  client.on('error', (err) => {
    console.error('Socket error:', err);
    frobd.kill();
    process.exit(1);
  });
  
  client.on('close', () => {
    console.log('Socket closed');
  });
  
}, 2000); // Wait 2 seconds for frobd to start

// Cleanup on exit
process.on('SIGINT', () => {
  console.log('\nCleaning up...');
  frobd.kill();
  process.exit(0);
});
