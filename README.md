# ZDTalkRecording
录制程序（早道（大连）教育科技有限公司）
### ZDTalkRecording 使用到以下项目
+ [Qt](http://www.qt.io/)
+ [obs-studio](https://github.com/obsproject/obs-studio)
+ [CrashRpt](http://crashrpt.sourceforge.net/)

### Qt 版本
- Qt 5.7.1 MSVC 2013 32bit

### OBS 修改记录
- 基于 obs-studio master 分支的 2017-05-22 22:51 110a9bd4 提交
- obs-studio 目录为 obs-studio 源码
- 修改内容如下，各个文件中标记有 "ZDTalk" 处为修改位置
    1. plugins/win-capture/window-capture.c
    2. plugins/win-capture/window-helpers.c
    3. libobs/obs.h
    4. libobs/obs-win-crash-handler.c
    5. libobs/windows.c
    
### CrashRpt 版本
- 1402