IF NOT EXIST "C:\vcpkg" git clone https://github.com/microsoft/vcpkg C:\vcpkg
call C:\vcpkg\bootstrap-vcpkg.bat
call C:\vcpkg\vcpkg integrate install
C:\vcpkg\vcpkg install librdkafka avro-cpp protobuf abseil utf8-range boost-property-tree boost-json snappy fmt curl --triplet x64-windows-static
REM ВАЖНО: avro-cpp ОБЯЗАН быть >= 1.12.1. В версиях ниже (подтверждено на 1.11.3) баг декодирования
REM глубоко-вложенных рекурсивных Avro-схем (segfault / vector range_check на проде). В 1.12.1 исправлено.
for /f "tokens=2" %%v in ('C:\vcpkg\vcpkg list avro-cpp ^| findstr /i avro-cpp') do set "AVROVER=%%v"
echo avro-cpp version: %AVROVER%
echo %AVROVER% | findstr /b /c:"1.12.1" /c:"1.12.2" /c:"1.12.3" /c:"1.12.4" /c:"1.12.5" /c:"1.12.6" /c:"1.12.7" /c:"1.12.8" /c:"1.12.9" /c:"1.13." /c:"1.14." /c:"1.15." /c:"2." >nul || (echo ERROR: avro-cpp %AVROVER% recursive-schema decode bug, need ^>= 1.12.1 & exit /b 1)
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
