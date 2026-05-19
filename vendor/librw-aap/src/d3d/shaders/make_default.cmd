@echo off
rem Build all default/im2d shaders as Shader Model 3.0. vs_3_0 / ps_3_0
rem give us 224 float constants, dynamic flow control, and the vPos register
rem which we rely on for the per-pixel lighting variants and the future
rem post-effects work.

"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /Fh default_amb_VS.h default_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DDIRECTIONALS /Fh default_amb_dir_VS.h default_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh default_all_VS.h default_VS.hlsl

"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /Fh default_PS.h default_PS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /DTEX /Fh default_tex_PS.h default_PS.hlsl

rem Per-pixel lighting variants (single RT — LDR path).
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /Fh default_pp_amb_VS.h default_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DDIRECTIONALS /Fh default_pp_amb_dir_VS.h default_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh default_pp_all_VS.h default_VS.hlsl

"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /DPER_PIXEL_LIGHTING /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh default_pp_PS.h default_PS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /DPER_PIXEL_LIGHTING /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /DTEX /Fh default_pp_tex_PS.h default_PS.hlsl

rem G-buffer variants (HDR-friendly, MRT slot 1 = packed normal+depth).
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DGBUFFER /Fh default_pp_gbuf_amb_VS.h default_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DGBUFFER /DDIRECTIONALS /Fh default_pp_gbuf_amb_dir_VS.h default_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /DPER_PIXEL_LIGHTING /DGBUFFER /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh default_pp_gbuf_all_VS.h default_VS.hlsl

"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /DPER_PIXEL_LIGHTING /DGBUFFER /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /Fh default_pp_gbuf_PS.h default_PS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /DPER_PIXEL_LIGHTING /DGBUFFER /DDIRECTIONALS /DPOINTLIGHTS /DSPOTLIGHTS /DTEX /Fh default_pp_gbuf_tex_PS.h default_PS.hlsl

"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T vs_3_0 /Fh im2d_VS.h im2d_VS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /Fh im2d_PS.h im2d_PS.hlsl
"%DXSDK_DIR%\utilities\bin\x86\fxc.exe" /nologo /T ps_3_0 /DTEX /Fh im2d_tex_PS.h im2d_PS.hlsl
