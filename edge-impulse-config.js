const EDGE_IMPULSE_CONFIG = {
    apiKey: '',
    projectId: '',
    ingestionUrl: 'https://ingestion.edgeimpulse.com/api/training/data',
    studioUrl: 'https://studio.edgeimpulse.com/v1/api'
  };
  
  document.addEventListener('DOMContentLoaded', function() {
    console.log('Edge Impulse corrected integration starting...');
    
    // html elements
    const apiKeyInput = document.getElementById('api-key');
    const projectIdInput = document.getElementById('project-id');
    const testConnectionBtn = document.getElementById('btn-test-connection');
    const connectionStatus = document.getElementById('ei-connection-status');
    const startCollectionBtn = document.getElementById('btn-start-collection');
    
    // test connection
    async function testEdgeImpulseConnection() {
      const apiKey = apiKeyInput.value.trim();
      const projectId = projectIdInput.value.trim();
      
      if (!apiKey || !projectId) {
        updateConnectionStatus(false, 'Please enter both API Key and Project ID');
        return;
      }
      
      testConnectionBtn.disabled = true;
      testConnectionBtn.textContent = 'Testing...';
      updateConnectionStatus(false, 'Testing connection...');
      
      try {
        // get proj info to verify connection
        let response = await fetch(`${EDGE_IMPULSE_CONFIG.studioUrl}/${projectId}/public-info`, {
          method: 'GET',
          headers: {
            'x-api-key': apiKey,
            'Content-Type': 'application/json'
          }
        });
        
        if (response.ok) {
          const projectInfo = await response.json();
          updateConnectionStatus(true, `Connected to "${projectInfo.name || 'Project ' + projectId}"`);
          EDGE_IMPULSE_CONFIG.apiKey = apiKey;
          EDGE_IMPULSE_CONFIG.projectId = projectId;
          if (startCollectionBtn) startCollectionBtn.disabled = false;
          return;
        }
        
        // info for private projects
        if (response.status === 404) {
          response = await fetch(`${EDGE_IMPULSE_CONFIG.studioUrl}/${projectId}/data-summary`, {
            method: 'GET',
            headers: {
              'x-api-key': apiKey,
              'Content-Type': 'application/json'
            }
          });
          
          if (response.ok) {
            updateConnectionStatus(true, `Connected to Project ${projectId} (Private)`);
            EDGE_IMPULSE_CONFIG.apiKey = apiKey;
            EDGE_IMPULSE_CONFIG.projectId = projectId;
            if (startCollectionBtn) startCollectionBtn.disabled = false;
            return;
          }
        }
        
        // handling errors
        if (response.status === 401 || response.status === 403) {
          updateConnectionStatus(false, 'Invalid API key');
        } else if (response.status === 404) {
          updateConnectionStatus(false, 'Project not found');
        } else {
          updateConnectionStatus(false, `API error: ${response.status}`);
        }
        
      } catch (error) {
        updateConnectionStatus(false, `Network error: ${error.message}`);
      } finally {
        testConnectionBtn.disabled = false;
        testConnectionBtn.textContent = 'Test Connection';
      }
    }
    
    // uploading data to EI
    // https://docs.edgeimpulse.com/reference/data-ingestion/data-acquisition-format
    async function uploadToEdgeImpulse(label, samples, sampleRate) {
      if (!EDGE_IMPULSE_CONFIG.apiKey) {
        throw new Error('Edge Impulse not configured');
      }
      
      if (!samples || samples.length === 0) {
        throw new Error('No samples to upload');
      }
      
      console.log(`Preparing to upload ${samples.length} samples with label "${label}" at ${sampleRate}Hz`);
      
      // formatting data
      const payload = {
        device_name: 'WebSerial-TouchSensor',
        device_type: 'XIAO_TOUCH_SENSOR',
        interval_ms: 1000 / sampleRate,
        sensors: [
          {
            name: 'touch_sensor',
            units: 'raw'
          }
        ],
        values: samples.map(sample => [sample.value])
      };
      
      const data = {
        protected: {
          ver: 'v1',
          alg: 'none'
        },
        signature: '0000000000000000000000000000000000000000000000000000000000000000',
        payload: payload
      };
      
      console.log('Data structure prepared:', {
        sampleCount: samples.length,
        intervalMs: payload.interval_ms,
        firstValue: samples[0]?.value,
        lastValue: samples[samples.length - 1]?.value
      });
      
      try {
        const response = await fetch(EDGE_IMPULSE_CONFIG.ingestionUrl, {
          method: 'POST',
          headers: {
            'x-api-key': EDGE_IMPULSE_CONFIG.apiKey,
            'x-label': label,
            'x-file-name': `${label}_${Date.now()}.json`,
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(data)
        });
        
        const responseText = await response.text();
        console.log('Edge Impulse response:', response.status, responseText);
        
        if (response.ok) {
          if (window.appendToSerialMonitor) {
            window.appendToSerialMonitor(`Data uploaded to Edge Impulse successfully!`, true);
            window.appendToSerialMonitor(`Uploaded ${samples.length} samples with label "${label}"`, true);
            window.appendToSerialMonitor(`Sample rate: ${sampleRate}Hz (${payload.interval_ms}ms interval)`, true);
          }
          console.log('Upload successful:', responseText);
          return { success: true, response: responseText };
        } else {
          throw new Error(`Upload failed: ${response.status} - ${responseText}`);
        }
      } catch (error) {
        if (window.appendToSerialMonitor) {
          window.appendToSerialMonitor(`Upload failed: ${error.message}`, true);
        }
        console.error('Upload error:', error);
        throw error;
      }
    }
    
    // update connection status
    function updateConnectionStatus(connected, message) {
      connectionStatus.textContent = message;
      connectionStatus.className = `status-indicator ${connected ? 'connected' : 'disconnected'}`;
    }
    
    document.addEventListener('dataCollectionComplete', async function(event) {
      console.log('Data collection completed, uploading to Edge Impulse...', event.detail);
      
      const { label, samples, sampleRate } = event.detail;
      
      if (samples.length === 0) {
        if (window.appendToSerialMonitor) {
          window.appendToSerialMonitor('No samples collected, skipping upload', true);
        }
        return;
      }
      
      if (!EDGE_IMPULSE_CONFIG.apiKey) {
        if (window.appendToSerialMonitor) {
          window.appendToSerialMonitor('Edge Impulse not connected, skipping upload', true);
        }
        return;
      }
      
      try {
        if (window.appendToSerialMonitor) {
          window.appendToSerialMonitor('Uploading to Edge Impulse...', true);
        }
        
        await uploadToEdgeImpulse(label, samples, sampleRate);
        
      } catch (error) {
        console.error('Failed to upload to Edge Impulse:', error);
      }
    });
    
    // event listeners
    testConnectionBtn.addEventListener('click', testEdgeImpulseConnection);
    
    // auto save credentials
    apiKeyInput.addEventListener('change', function() {
      localStorage.setItem('ei_api_key', apiKeyInput.value);
    });
    
    projectIdInput.addEventListener('change', function() {
      localStorage.setItem('ei_project_id', projectIdInput.value);
    });
    
    // load credentials
    const savedApiKey = localStorage.getItem('ei_api_key');
    const savedProjectId = localStorage.getItem('ei_project_id');
    
    if (savedApiKey) apiKeyInput.value = savedApiKey;
    if (savedProjectId) projectIdInput.value = savedProjectId;
    
    window.EdgeImpulse = {
      isConnected: () => !!(EDGE_IMPULSE_CONFIG.apiKey),
      upload: uploadToEdgeImpulse,
      getConfig: () => ({ ...EDGE_IMPULSE_CONFIG })
    };
    
    console.log('Edge Impulse corrected integration ready');
  });