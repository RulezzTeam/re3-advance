@echo off
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /Fh skin_amb_VS.h skin_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DDIRECTIONALS /Fh skin_amb_dir_VS.h skin_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh skin_all_VS.h skin_VS.hlsl

rem Per-pixel lighting variants for skinned meshes.
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /Fh skin_pp_amb_VS.h skin_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DDIRECTIONALS /Fh skin_pp_amb_dir_VS.h skin_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh skin_pp_all_VS.h skin_VS.hlsl
