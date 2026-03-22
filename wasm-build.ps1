$EMSDK = "C:\env\emsdk"
$env:EMSDK_NODE = "$EMSDK\node\22.16.0_64bit\bin\node.exe"
$env:EMSDK_PYTHON = "$EMSDK\python\3.13.3_64bit\python.exe"
$env:PATH = "$EMSDK;$EMSDK\upstream\emscripten;$env:PATH"

Set-Location "C:\Users\ftp\WeChatProjects\spt-2d"

Write-Host "========================================"
Write-Host "spt-2d WASM Build Script"
Write-Host "========================================"

if ($args.Count -eq 0) {
    Write-Host "Usage: .\wasm-build.ps1 [wx|web|all]"
    Write-Host "  wx   - Build for WeChat Mini Game"
    Write-Host "  web  - Build for Web (Emscripten)"
    Write-Host "  all  - Build both targets"
    exit 0
}

function Build-Wx {
    Write-Host ""
    Write-Host "[Building for WeChat Mini Game...]"
    
    if (-not (Test-Path "build-wx")) {
        New-Item -ItemType Directory -Path "build-wx" | Out-Null
    }
    
    em++ src/Main.cpp src/platform/wx/WxPlatform.cpp `
        -D__WXGAME__ -D__EMSCRIPTEN__ `
        -Isrc -Isrc/core -Isrc/platform `
        -sWASM=1 -sMODULARIZE=1 -sEXPORT_NAME=GameModule -sINVOKE_RUN=0 `
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_WxBridge_OnTouch','_WxBridge_OnKey','_WxBridge_OnMouse','_WxBridge_OnResize','_WxBridge_OnSafeAreaChange','_WxBridge_OnHttpResponse','_WxBridge_OnAppShow','_WxBridge_OnAppHide','_WxBridge_OnError','_WxBridge_OnMemoryWarning']" `
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','stringToUTF8','UTF8ToString','lengthBytesUTF8','getValue','setValue']" `
        -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216 `
        -sNO_EXIT_RUNTIME=1 -sEXPORT_ES6=0 -sUSE_ES6_IMPORT_META=0 `
        -sDYNAMIC_EXECUTION=0 -sFILESYSTEM=0 `
        -O2 -o build-wx/game-wasm.js
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build FAILED with exit code: $LASTEXITCODE" -ForegroundColor Red
        return $false
    }
    
    Write-Host "WeChat Mini Game build finished successfully." -ForegroundColor Green
    Write-Host ""
    Write-Host "[Copying to wxgame directory...]"
    
    Copy-Item -Path "build-wx\game-wasm.js" -Destination "C:\Users\ftp\WeChatProjects\wxgame\game-wasm.js" -Force
    Copy-Item -Path "build-wx\game-wasm.wasm" -Destination "C:\Users\ftp\WeChatProjects\wxgame\game-wasm.wasm" -Force
    
    Write-Host "Files copied to wxgame directory." -ForegroundColor Green
    return $true
}

function Build-Web {
    Write-Host ""
    Write-Host "[Building for Web...]"
    
    if (-not (Test-Path "build-web")) {
        New-Item -ItemType Directory -Path "build-web" | Out-Null
    }
    
    em++ src/Main.cpp src/platform/web/WebPlatform.cpp `
        -D__EMSCRIPTEN__ `
        -Isrc -Isrc/core -Isrc/platform `
        -sWASM=1 `
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']" `
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','stringToUTF8','UTF8ToString','lengthBytesUTF8']" `
        -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216 `
        -sUSE_WEBGL2=1 -sFULL_ES3=1 `
        -O2 -o build-web/game.js
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build FAILED with exit code: $LASTEXITCODE" -ForegroundColor Red
        return $false
    }
    
    Copy-Item -Path "index.html" -Destination "build-web\index.html" -Force
    Write-Host "Web build finished successfully." -ForegroundColor Green
    return $true
}

switch ($args[0]) {
    "wx" {
        Build-Wx
    }
    "web" {
        Build-Web
    }
    "all" {
        Build-Wx
        Build-Web
    }
    default {
        Write-Host "Unknown target: $($args[0])" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "Build complete!"
Write-Host "========================================"
