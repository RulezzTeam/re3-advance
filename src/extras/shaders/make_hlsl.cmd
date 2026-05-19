@echo off
rem All postfx shaders target ps_3_0 / vs_3_0 — gives us proper [loop],
rem 12+ texture samples, and 512 ALU headroom for the bigger composes
rem (hdrResolve + volumetric fog, ssao, taa). ps_3_0 has wider driver
rem support than ps_2_b on modern hardware.
for %%f in (*PS.hlsl) do "%DXSDK_DIR%\Utilities\bin\x86\fxc.exe" /T ps_3_0 /nologo /E main /Fo obj\%%~nf.cso %%f
for %%f in (*VS.hlsl) do "%DXSDK_DIR%\Utilities\bin\x86\fxc.exe" /T vs_3_0 /nologo /E main /Fo obj\%%~nf.cso %%f
