## scrcpy-for-VisualStudio

### 由来

大家都非常的熟悉 https://github.com/Genymobile/scrcpy `scrcpy` 是一个利用usb来投屏到电脑的程序，程序的话非常的强大，但是编译并不友好，调试起来有比较大的麻烦。https://github.com/fj1981/scrcpy-vs ，`scrcpy-vs`把`scrcpy`的编译移植到visual studio上，让`scrcpy` 调试起来非常的简单。`scrcpy-vs` 在github上，还是缺少了一些文件，配置起来需要时间，这里把缺失的文件补充上来。

### 编译

使用vs2019进行编译就可以。

### 问题

在编译的时候，缺少了对项目过程的了解，后面碰到问题无法进行排查，可以把`scrcpy`分为两个阶段，一个是手机端的启动过程，一个是界面和控制，手机端的启动过程比较容易出错。

#### 手机端的启动过程

把 `jar` 拷贝到手机中：

    adb push "adb/scrcpy-server" "/data/local/tmp/scrcpy-server.jar"
    
映射端口：

    adb reverse localabstract:scrcpy tcp:27183
    
`adb`启动`jar`:

    adb shell CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server 1.12.1 0 8000000 0 false - true true
    
其中必须要注意的是，`scrcpy-server` 的路径和版本要正确。

![](https://github.com/go2coding/scrcpy-for-VisualStudio/raw/master/scrcpy-vs/images.gif)