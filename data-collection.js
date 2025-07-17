// vars
let isCollecting = false;
let collectionData = [];
let collectionStartTime = null;
let collectionTimer = null;

document.addEventListener('DOMContentLoaded', function() {
  console.log('Data collection initializing...');
  
  // html elements
  const sampleLabelInput = document.getElementById('sample-label');
  const collectionDurationInput = document.getElementById('collection-duration');
  const sampleRateInput = document.getElementById('sample-rate');
  const startCollectionBtn = document.getElementById('btn-start-collection');
  const stopCollectionBtn = document.getElementById('btn-stop-collection');
  
  if (!sampleLabelInput || !collectionDurationInput || !sampleRateInput || 
      !startCollectionBtn || !stopCollectionBtn) {
    console.error('Data collection elements not found in DOM');
    return;
  }
  
  console.log('Data collection elements found, setting up event listeners...');
  
  // start data collection
  async function startDataCollection() {
    const label = sampleLabelInput.value.trim();
    const duration = parseInt(collectionDurationInput.value);
    const sampleRate = parseInt(sampleRateInput.value);
    
    // error handling
    if (!label) {
      alert('Please enter a sample label');
      return;
    }
    
    if (duration < 1000 || duration > 30000) {
      alert('Duration must be between 1000ms and 30000ms');
      return;
    }
    
    if (sampleRate < 10 || sampleRate > 1000) {
      alert('Sample rate must be between 10Hz and 1000Hz');
      return;
    }
    
    if (!window.port || !window.port.readable) {
      alert('Please connect to a serial device first');
      return;
    }
    
    console.log(`Starting data collection: ${label}, ${duration}ms, ${sampleRate}Hz`);
    
    // init collection
    isCollecting = true;
    collectionData = [];
    collectionStartTime = Date.now();
    
    // updating ui
    startCollectionBtn.disabled = true;
    stopCollectionBtn.disabled = false;
    startCollectionBtn.textContent = `Collecting "${label}"...`;
    
    // debugging
    if (window.appendToSerialMonitor) {
      window.appendToSerialMonitor(`Started collecting "${label}" for ${duration}ms at ${sampleRate}Hz`, true);
    }
    
    // set timer
    collectionTimer = setTimeout(() => {
      stopDataCollection();
    }, duration);
  }
  
  // stop data collection
  function stopDataCollection() {
    if (!isCollecting) return;
    
    console.log('Stopping data collection...');
    
    isCollecting = false;
    
    // clear timer
    if (collectionTimer) {
      clearTimeout(collectionTimer);
      collectionTimer = null;
    }
    
    // update ui
    startCollectionBtn.disabled = false;
    stopCollectionBtn.disabled = true;
    startCollectionBtn.textContent = 'Start Data Collection';
    
    const collectionDuration = Date.now() - collectionStartTime;
    const sampleCount = collectionData.length;
    const actualSampleRate = sampleCount / (collectionDuration / 1000);
    
    if (window.appendToSerialMonitor) {
      window.appendToSerialMonitor(`Collection stopped. Collected ${sampleCount} samples in ${collectionDuration}ms (${actualSampleRate.toFixed(1)}Hz)`, true);
    }
    
    // process and upload data
    if (collectionData.length > 0) {
      const label = sampleLabelInput.value.trim();
      const targetSampleRate = parseInt(sampleRateInput.value);
      
      processCollectedData(label, collectionData, targetSampleRate);
    } else {
      if (window.appendToSerialMonitor) {
        window.appendToSerialMonitor('No data collected', true);
      }
    }
  }
  
  function processCollectedData(label, samples, sampleRate) {
    console.log(`Processing ${samples.length} samples for label "${label}"`);
    
    if (window.appendToSerialMonitor) {
      window.appendToSerialMonitor(`Processing ${samples.length} samples...`, true);
      
      // debugging
      if (samples.length > 0) {
        const firstSample = samples[0];
        const lastSample = samples[samples.length - 1];
        
        if (typeof firstSample === 'object' && 'ax' in firstSample) {
          // multi sensor data
          window.appendToSerialMonitor(`Sample format: Multi-sensor (ax=${firstSample.ax}, ay=${firstSample.ay}, az=${firstSample.az}...)`, true);
        } else if (typeof firstSample === 'object' && 'value' in firstSample) {
          // single sensor
          window.appendToSerialMonitor(`Sample values: [${firstSample.value.toFixed(2)}] ... [${lastSample.value.toFixed(2)}]`, true);
        } else if (typeof firstSample === 'number') {
          // other direct num values
          window.appendToSerialMonitor(`Sample values: [${firstSample.toFixed(2)}] ... [${lastSample.toFixed(2)}]`, true);
        }
      }
    }
    
    // collection event
    const collectionCompleteEvent = new CustomEvent('dataCollectionComplete', {
      detail: {
        label: label,
        samples: samples,
        sampleRate: sampleRate,
        collectionTime: Date.now(),
        duration: samples.length > 0 ? (samples[samples.length - 1].timestamp - samples[0].timestamp) : 0
      }
    });
    
    // send event to edge impulse to handle
    document.dispatchEvent(collectionCompleteEvent);
    
    console.log('Data collection complete event dispatched');
  }
  
  // adding data point during collection - FIXED VERSION
  function addDataPoint(value, timestamp = Date.now()) {
    if (!isCollecting) return;
    
    let dataPoint;
  
    if (typeof value === 'number') {
      // single val
      dataPoint = {
        value: value,
        timestamp: timestamp
      };
    } else if (typeof value === 'object' && value !== null) {
      // multiple sensors - preserve the original structure and add timestamp
      dataPoint = {
        ...value,
        timestamp: timestamp
      };
    } else {
      console.warn('Invalid data format received:', value);
      return;
    }
    
    collectionData.push(dataPoint);
    
    // debugging: log every 50 samples
    if (collectionData.length % 50 === 0) {
      console.log(`Collection progress: ${collectionData.length} samples`);
      if (window.appendToSerialMonitor) {
        window.appendToSerialMonitor(`Progress: ${collectionData.length} samples collected`, true);
      }
    }
  }
  
  // event listeners
  startCollectionBtn.addEventListener('click', startDataCollection);
  stopCollectionBtn.addEventListener('click', stopDataCollection);
  
  window.DataCollection = {
    addDataPoint: addDataPoint,
    isCollecting: () => isCollecting,
    getCurrentData: () => [...collectionData],
    stop: stopDataCollection
  };
  
  console.log('Data collection system ready');
});

function captureDataForCollection(serialData) {
  if (!window.DataCollection || !window.DataCollection.isCollecting()) {
    return;
  }
  
  console.log('Capturing data for collection:', serialData);
  
  const lines = serialData.split('\n');
  for (const line of lines) {
    const trimmedLine = line.trim();
    if (trimmedLine === '') continue;
    
    // skip header line
    if (trimmedLine.includes('timestamp') || trimmedLine.includes('Ax')) {
      continue;
    }
    
    // parse CSV data
    if (trimmedLine.includes(',')) {
      const values = trimmedLine.split(',').map(v => parseFloat(v.trim()));
      
      // format: timestamp,Ax,Ay,Az,Dx,Dy,flex,B1,B2
      if (values.length >= 9 && values.every(v => !isNaN(v))) {
        const sensorData = {
          timestamp: values[0],
          ax: values[1],
          ay: values[2],
          az: values[3],
          dx: values[4],
          dy: values[5],
          flex: values[6],
          b1: values[7],
          b2: values[8]
        };
        
        console.log('Found multi-sensor data:', sensorData);
        window.DataCollection.addDataPoint(sensorData);
        continue;
      }
    }
    
    // simple number
    const simpleNumber = parseFloat(trimmedLine);
    if (!isNaN(simpleNumber)) {
      console.log('Found simple number:', simpleNumber);
      window.DataCollection.addDataPoint(simpleNumber);
      continue;
    }
    
    // JSON parsing
    try {
      const jsonData = JSON.parse(trimmedLine);
      const possibleKeys = ['value', 'sensor', 'touch', 'reading', 'data'];
      for (const key of possibleKeys) {
        if (typeof jsonData[key] === 'number') {
          console.log('Found JSON value:', jsonData[key]);
          window.DataCollection.addDataPoint(jsonData[key]);
          break;
        }
      }
    } catch (e) {

    }
  }
}

window.captureDataForCollection = captureDataForCollection;