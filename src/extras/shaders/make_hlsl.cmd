@echo off
rem fxaa_PS doesn't fit in ps_2_0 (65 ALU > 64); compile it as ps_2_b first.
"%DXSDK_DIR%\Utilities\bin\x86\fxc.exe" /T ps_2_b /nologo /E main /Fo obj\fxaa_PS.cso fxaa_PS.hlsl
for %%f in (*PS.hlsl) do (
    if /I not "%%~nf"=="fxaa_PS" "%DXSDK_DIR%\Utilities\bin\x86\fxc.exe" /T ps_2_0 /nologo /E main /Fo obj\%%~nf.cso %%f
)
for %%f in (*VS.hlsl) do "%DXSDK_DIR%\Utilities\bin\x86\fxc.exe" /T vs_2_0 /nologo /E main /Fo obj\%%~nf.cso %%f
