// Deliberate test-only worker exception; never loaded by the Reader UI.
self.postMessage({type:'ready'});
self.onmessage = () => { throw new Error('intentional-test-worker-failure'); };
