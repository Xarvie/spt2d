if (typeof window === 'undefined') { globalThis.window = globalThis; }
if (typeof document === 'undefined') { globalThis.document = {}; }
if (typeof WebAssembly === 'undefined' && typeof WXWebAssembly !== 'undefined') {
    globalThis.WebAssembly = WXWebAssembly;
}

if (typeof performance === 'undefined') {
    var startTime = Date.now();
    globalThis.performance = {
        now: function() {
            return Date.now() - startTime;
        },
        timeOrigin: startTime
    };
}
