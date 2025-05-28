// html elements
const connectButton = document.getElementById("btn-connect");
const clearButton = document.getElementById("btn-clear");
const serialDataElement = document.getElementById("serialData");
const autoscrollCheckbox = document.getElementById("autoscroll");
const connectionStatus = document.getElementById("connection-status");
const sendButton = document.getElementById("btn-send");
const sendLineButton = document.getElementById("btn-send-line");
const serialInput = document.getElementById("serial-input");

// global vars
let port = null;
let reader = null;
let keepReading = true;
window.port = null;

// ***************************************************************************************

// formatting data
function appendToSerialMonitor(data, isStatus = false) {
  const timestamp = new Date().toLocaleTimeString();
  const dataLine = document.createElement("div");
  dataLine.className = "data-line";
  
  const timestampSpan = document.createElement("span");
  timestampSpan.className = "timestamp";
  timestampSpan.textContent = `[${timestamp}]`;
  
  const contentSpan = document.createElement("span");
  contentSpan.className = isStatus ? "status-message" : "data-message";
  contentSpan.textContent = data;
  
  dataLine.appendChild(timestampSpan);
  dataLine.appendChild(contentSpan);
  
  // line
  serialDataElement.appendChild(dataLine);
  
  // auto scroll
  if (autoscrollCheckbox.checked) {
    serialDataElement.scrollTop = serialDataElement.scrollHeight;
  }
}

window.appendToSerialMonitor = appendToSerialMonitor;

// updating connection status
function updateConnectionStatus(connected) {
  connectionStatus.textContent = connected ? "Connected" : "Disconnected";
  connectionStatus.className = connected ? "connected" : "";
  connectButton.textContent = connected ? "Disconnect" : "Connect Device";

  sendButton.disabled = !connected;
  sendLineButton.disabled = !connected;
  serialInput.disabled = !connected;
}

// disconnecting
async function disconnectFromPort() {
  // flag to stop reading
  keepReading = false;
  
  try {
    if (reader) {
      await reader.cancel();
    }
    
    // small delay to let reader finish
    await new Promise(resolve => setTimeout(resolve, 100));
    
    // close the port
    if (port) {
      await port.close();
      port = null;
      window.port = null;
    }
    
    updateConnectionStatus(false);
    appendToSerialMonitor("Disconnected from device", true);
  } catch (error) {
    console.error("Error during disconnect:", error);
    appendToSerialMonitor(`Disconnect error: ${error.message}`, true);
    
    reader = null;
    port = null;
    window.port = null;
    updateConnectionStatus(false);
  }
}

// serial port connection listeners
navigator.serial.addEventListener("connect", (e) => {
  console.log("Device connected:", e.target);
  appendToSerialMonitor("Device connected", true);
});

navigator.serial.addEventListener("disconnect", (e) => {
  console.log("Device disconnected:", e.target);
  appendToSerialMonitor("Device disconnected", true);
  
  if (port && e.target === port) {
    port = null;
    window.port = null;
    reader = null;
    updateConnectionStatus(false);
  }
});

// init all avail ports
navigator.serial.getPorts().then((ports) => {
  console.log("Available ports:", ports);
  if (ports.length > 0) {
    appendToSerialMonitor(`${ports.length} serial device(s) available`, true);
  }
});

// debounce on connection btn
let isHandlingConnection = false;
connectButton.addEventListener("click", async () => {
  if (isHandlingConnection) return;
  isHandlingConnection = true;
  
  try {
    if (port) {
      await disconnectFromPort();
      isHandlingConnection = false;
      return;
    }
    
    const usbVendorId = 0x2886;
    
    port = await navigator.serial.requestPort({ 
      filters: [{ usbVendorId }] 
    });
    
    console.log("Selected port:", port);
    appendToSerialMonitor("Device selected. Opening connection...", true);
    
    // open port
    await port.open({ 
      baudRate: 115200
    });
    
    // Update global reference
    window.port = port;
    
    updateConnectionStatus(true);
    appendToSerialMonitor("Connection established", true);
    
    keepReading = true;
    
    // reading data
    readSerialData();
    
  } catch (error) {
    console.error("Error with serial port:", error);
    appendToSerialMonitor(`Error: ${error.message}`, true);
    await disconnectFromPort();
  } finally {
    isHandlingConnection = false;
  }
});

// clear btn
clearButton.addEventListener("click", () => {
  serialDataElement.innerHTML = "";
  appendToSerialMonitor("Monitor cleared", true);
});

// read serial data
async function readSerialData() {
  try {
    while (port && port.readable && keepReading) {
      reader = port.readable.getReader();
      
      try {
        while (keepReading) {
          const { value, done } = await reader.read();
          
          if (done) {
            console.log("Reader reported done");
            break;
          }
          
          const decodedData = new TextDecoder().decode(value);
          console.log("Received data:", decodedData);
          
          // display data
          appendToSerialMonitor(decodedData);
          
          // pass data to collection system
          if (window.captureDataForCollection) {
            window.captureDataForCollection(decodedData);
          }
        }
      } catch (error) {
        console.error("Error while reading:", error);
        appendToSerialMonitor(`Read error: ${error.message}`, true);
        
        if (!keepReading || !port || !port.readable) {
          break;
        }
      } finally {
        if (reader) {
          try {
            reader.releaseLock();
          } catch (e) {
            console.error("Error releasing lock:", e);
          }
          reader = null;
        }
      }
      
      if (keepReading && port && port.readable) {
        await new Promise(resolve => setTimeout(resolve, 100));
      }
    }
  } catch (error) {
    console.error("Fatal error in read loop:", error);
    appendToSerialMonitor(`Fatal error: ${error.message}`, true);
  } finally {
    console.log("Exiting read loop");
    
    if (port && keepReading) {
      keepReading = false;
      disconnectFromPort();
    }
  }
}

// write serial
async function sendSerialData(data, addLineEnd = false) {
  if (!port || !port.writable) {
    appendToSerialMonitor("Error: no active connection", true);
    return;
  }
  
  try {
    const writer = port.writable.getWriter();
    
    // add line
    const dataToSend = addLineEnd ? data + '\n' : data;
    const encodedData = new TextEncoder().encode(dataToSend);
    
    await writer.write(encodedData);
    writer.releaseLock();
    
    // debug
    appendToSerialMonitor(`${dataToSend}`, true);
  }
  
  catch (error) {
    console.error("Error sending data:", error);
    appendToSerialMonitor('Send error: ${error.message}', true);
  }
}

sendButton.addEventListener("click", () =>{
  const data = serialInput.value;
  if (data) {
    sendSerialData(data);
  }
});

sendLineButton.addEventListener("click", () => {
  const data = serialInput.value;
  if (data) {
    sendSerialData(data);
  }
});

window.addEventListener('beforeunload', () => {
  if (port) {
    disconnectFromPort();
  }
});