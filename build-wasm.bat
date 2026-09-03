cd dist\web
cmake --build . -j8

cd ..\..
mkdir public\js
set current_dir=%~dp0
copy "%current_dir%dist\web\app.wasm" "%current_dir%public\js"
copy "%current_dir%dist\web\app.js" "%current_dir%public\js"
