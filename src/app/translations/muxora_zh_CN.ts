<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>Muxora</name>
    <message>
        <source>Muxora window system preview</source>
        <translation>Muxora 窗口系统预览</translation>
    </message>
    <message>
        <source>Use the initial layout without loading saved state</source>
        <translation>使用初始布局，不读取保存状态</translation>
    </message>
    <message>
        <source>Exit automatically after the startup check</source>
        <translation>启动检查后自动退出</translation>
    </message>
    <message>
        <source>Save a window screenshot after startup</source>
        <translation>启动后保存窗口截图</translation>
    </message>
    <message>
        <source>Run a registered command after startup</source>
        <translation>启动后执行已注册命令</translation>
    </message>
    <message>
        <source>Use a separate layout state directory</source>
        <translation>指定独立布局状态目录</translation>
    </message>
    <message>
        <source>Show the windows with Qt Quick (in development; the layout is neither restored nor saved)</source>
        <translation>用 Qt Quick 显示窗口（开发中，不恢复也不保存布局）</translation>
    </message>
    <message>
        <source>New workspace tab</source>
        <translation>新建工作区标签</translation>
    </message>
    <message>
        <source>Workspace %1</source>
        <translation>工作区 %1</translation>
    </message>
    <message>
        <source>Files</source>
        <translation>文件浏览</translation>
    </message>
    <message>
        <source>Sessions</source>
        <translation>会话</translation>
    </message>
    <message>
        <source>Transfers</source>
        <translation>传输队列</translation>
    </message>
    <message>
        <source>New %1 tab</source>
        <translation>新建%1标签</translation>
    </message>
    <message>
        <source>Save workspace layout</source>
        <translation>保存工作区布局</translation>
    </message>
    <message>
        <source>Layout saved</source>
        <translation>布局已保存</translation>
    </message>
    <message>
        <source>Failed to save layout</source>
        <translation>布局保存失败</translation>
    </message>
    <message>
        <source>Exit</source>
        <translation>退出</translation>
    </message>
    <message>
        <source>Failed to save layout; application remains open</source>
        <translation>布局保存失败，未退出</translation>
    </message>
    <message>
        <source>Reset workspace layout</source>
        <translation>恢复初始工作区布局</translation>
    </message>
    <message>
        <source>About Muxora</source>
        <translation>关于 Muxora</translation>
    </message>
    <message>
        <source>Muxora 0.1.0
First window system preview

Qt / Native C++, open source under MIT.
Remote connections and file operations will follow after the window system is ready.</source>
        <translation>Muxora 0.1.0
窗口系统首个预览

Qt / 原生 C++，MIT 开源。
远程连接与文件功能将在窗口系统确定后接入。</translation>
    </message>
    <message>
        <source>Application services were closed</source>
        <translation>应用服务已关闭</translation>
    </message>
    <message>
        <source>Lua script running</source>
        <translation>Lua 脚本运行中</translation>
    </message>
    <message>
        <source>Click to stop running Lua scripts</source>
        <translation>点击停止正在运行的 Lua 脚本</translation>
    </message>
    <message>
        <source>The script is still running. A stop has been requested.</source>
        <translation>脚本仍在运行，已请求停止。</translation>
    </message>
    <message>
        <source>Failed to load Lua configuration: %1</source>
        <translation>Lua 配置加载失败：%1</translation>
    </message>
    <message>
        <source>Run Lua script…</source>
        <translation>运行 Lua 脚本…</translation>
    </message>
    <message>
        <source>Stop running Lua scripts</source>
        <translation>停止正在运行的 Lua 脚本</translation>
    </message>
    <message>
        <source>Reload Lua configuration</source>
        <translation>重新加载 Lua 配置</translation>
    </message>
    <message>
        <source>Open Lua configuration</source>
        <translation>打开 Lua 配置文件</translation>
    </message>
    <message>
        <source>Could not open the Lua configuration file.</source>
        <translation>无法打开 Lua 配置文件。</translation>
    </message>
    <message>
        <source>Could not restore the default keybindings before loading scripts.</source>
        <translation>无法恢复脚本加载前的默认快捷键。</translation>
    </message>
    <message>
        <source>Lua script exceeds the %1 MiB transport limit.</source>
        <translation>Lua 脚本超过 %1 MiB 传输限制。</translation>
    </message>
    <message>
        <source>Lua scripts must use UTF-8 encoding.</source>
        <translation>Lua 脚本必须使用 UTF-8 编码。</translation>
    </message>
    <message>
        <source>Provide either code or file when running a script, not both.</source>
        <translation>运行脚本时只能提供 code 或 file 其中一个。</translation>
    </message>
    <message>
        <source>Lua script parameters must be strings.</source>
        <translation>Lua 脚本参数必须是字符串。</translation>
    </message>
    <message>
        <source>Run Lua script</source>
        <translation>运行 Lua 脚本</translation>
    </message>
    <message>
        <source>Enter Lua script:</source>
        <translation>输入 Lua 脚本：</translation>
    </message>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Could not create the Lua configuration directory.</source>
        <translation>无法创建 Lua 配置目录。</translation>
    </message>
    <message>
        <source>-- User/init.lua runs after the workspace has been restored.
-- Put pure Lua modules in User/lua, then use require(&apos;module_name&apos;).
-- Reload from Tools &gt; Lua after saving this file.

muxora.keymap.set(&apos;ctrl+alt+l&apos;, function(context)
  muxora.ui.notify({ title = &apos;Muxora&apos;, text = &apos;Lua is ready.&apos;, timeout_ms = 3000 })
end, { id = &apos;user.hello&apos;, title = &apos;Lua: Hello&apos; })
</source>
        <translation>-- 工作区恢复后运行 User/init.lua。
-- 将纯 Lua 模块放入 User/lua，使用 require(&apos;module_name&apos;) 加载。
-- 保存文件后，从工具 &gt; Lua 重新加载。

muxora.keymap.set(&apos;ctrl+alt+l&apos;, function(context)
  muxora.ui.notify({ title = &apos;Muxora&apos;, text = &apos;Lua 已就绪。&apos;, timeout_ms = 3000 })
end, { id = &apos;user.hello&apos;, title = &apos;Lua: 问候&apos; })
</translation>
    </message>
    <message>
        <source>Script services have been closed.</source>
        <translation>脚本服务已关闭。</translation>
    </message>
    <message>
        <source>Notifications require text; the message may contain up to 8192 characters and the title up to 256.</source>
        <translation>通知需要文本；正文最多 8192 个字符，标题最多 256 个字符。</translation>
    </message>
    <message>
        <source>Notification duration must be between 0 and 600000 milliseconds.</source>
        <translation>通知时长必须是 0 到 600000 之间的毫秒数。</translation>
    </message>
    <message>
        <source>At most 16 script notifications may be shown at once.</source>
        <translation>最多同时显示 16 条脚本通知。</translation>
    </message>
    <message>
        <source>Launching a program requires valid program and cwd strings.</source>
        <translation>启动程序需要有效的 program 和 cwd 字符串。</translation>
    </message>
    <message>
        <source>Program args must be an array of strings.</source>
        <translation>程序参数 args 必须是字符串数组。</translation>
    </message>
    <message>
        <source>Program arguments exceed the count or length limit.</source>
        <translation>程序参数超过数量或长度限制。</translation>
    </message>
    <message>
        <source>At most 8 program launch requests may be pending at once.</source>
        <translation>最多同时处理 8 个程序启动请求。</translation>
    </message>
    <message>
        <source>Sound must be beep or the path to a local WAV file.</source>
        <translation>声音参数需要 beep 或本地 WAV 文件路径。</translation>
    </message>
    <message>
        <source>Sound must be a local WAV file no larger than 16 MiB.</source>
        <translation>声音文件必须是最多 16 MiB 的本地 WAV 文件。</translation>
    </message>
    <message>
        <source>Could not play the sound file.</source>
        <translation>无法播放声音文件。</translation>
    </message>
    <message>
        <source>Only the beep sound is supported on this platform.</source>
        <translation>当前平台仅支持 beep 提示音。</translation>
    </message>
    <message>
        <source>Unknown script service: %1</source>
        <translation>未知脚本服务：%1</translation>
    </message>
    <message>
        <source>Muxora / Workspace notes

This is a local editor. Type here and check that content survives moving between windows.
SSH and a real terminal will follow after the window system is ready.

Try these operations:
  · Drag a tab onto the center or direction guide of a target group
  · Drag to the desktop to create a floating window, then drag its title bar to dock it again
  · Right-click a tab to auto-hide it in any sidebar or split its panes

Ctrl+Shift+P     Command palette
Ctrl+Shift+E     Split pane right
Ctrl+Shift+O     Split pane down
Ctrl+Alt+Arrow   Navigate tab groups
Alt+Arrow        Navigate panes
Ctrl+Shift+D     Move to floating window
</source>
        <translation>Muxora / 工作区草稿

这是一块本地编辑区域，可以输入文字并观察移动后状态是否保留。
SSH 与真正的终端会在窗口系统确定后接入。

试试这些操作：
  · 拖动上面的标签到目标组的中央或方向指示
  · 拖到桌面形成轻量浮窗，再拖标题栏合并回来
  · 右键标签，收纳到任意边栏或拆分内部窗格

Ctrl+Shift+P     命令面板
Ctrl+Shift+E     向右拆分窗格
Ctrl+Shift+O     向下拆分窗格
Ctrl+Alt+方向    标签组导航
Alt+方向        内部窗格导航
Ctrl+Shift+D     移到轻量浮窗
</translation>
    </message>
    <message>
        <source>Sessions · Layout preview</source>
        <translation>会话区域 · 布局预览</translation>
    </message>
    <message>
        <source>Development server</source>
        <translation>开发服务器</translation>
    </message>
    <message>
        <source>Test server</source>
        <translation>测试服务器</translation>
    </message>
    <message>
        <source>Personal host</source>
        <translation>个人主机</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <source>Directory</source>
        <translation>目录</translation>
    </message>
    <message>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Transfer list

File transfer is not connected yet.
This tab uses the same docking and window operations as other tabs.</source>
        <translation>传输列表区域

文件传输尚未接入。
这个标签与其他标签使用同一套停靠和窗口操作。</translation>
    </message>
    <message>
        <source>Arrange remote workspaces your way</source>
        <translation>让远程工作区按你的习惯排列</translation>
    </message>
    <message>
        <source>This is the first Muxora window system preview.

Drag tabs, arrange tab groups, split panes, and auto-hide tabs in sidebars.
Detach a floating window and dock it into another window.

Sessions, files, and transfers are layout placeholders. Remote operations will follow later.</source>
        <translation>这是 Muxora 的首个窗口系统预览。

可以拖动标签、自由分组、拆分窗格、收纳边栏，
也可以拖出轻量浮窗，再合并到其他窗口。

会话、文件和传输区域目前用于试用布局，实际远程功能将在后续接入。</translation>
    </message>
    <message>
        <source>Open a workspace tab</source>
        <translation>打开一个工作区标签</translation>
    </message>
    <message>
        <source>Show all commands</source>
        <translation>查看所有命令</translation>
    </message>
    <message>
        <source>Adjust window behavior</source>
        <translation>调整窗口行为</translation>
    </message>
    <message>
        <source>Right-click a tab or floating window title bar for more layout operations.</source>
        <translation>右键标签或浮窗标题栏，可以找到更多布局操作。</translation>
    </message>
    <message>
        <source>Welcome</source>
        <translation>欢迎</translation>
    </message>
    <message>
        <source>Workspace 1</source>
        <translation>工作区 1</translation>
    </message>
    <message>
        <source>/workspace · File view preview</source>
        <translation>/workspace · 文件视图预览</translation>
    </message>
    <message numerus="yes">
        <source>%n items  ·  File operations are not connected yet</source>
        <translation>
            <numerusform>%n 项  ·  文件功能尚未接入</numerusform>
        </translation>
    </message>
    <message>
        <source>tab appearance</source>
        <translation>标签外观</translation>
    </message>
    <message>
        <source>window behavior</source>
        <translation>窗口行为</translation>
    </message>
    <message>
        <source>default bar visibility</source>
        <translation>各栏的默认显隐</translation>
    </message>
    <message>
        <source>default toolbar layout</source>
        <translation>默认工具条排布</translation>
    </message>
    <message>
        <source>The file could not be copied and will be replaced at the next save.</source>
        <translation>无法复制原文件，下次保存时会被覆盖。</translation>
    </message>
    <message>
        <source>The original file was kept as %1.</source>
        <translation>原文件已保存为 %1。</translation>
    </message>
    <message>
        <source>the file exceeds 4 MiB</source>
        <translation>文件超过 4 MiB</translation>
    </message>
    <message>
        <source>the file does not contain a JSON object</source>
        <translation>文件内容不是 JSON 对象</translation>
    </message>
    <message>
        <source>The saved layout could not be read (%1), so the default layout is shown.</source>
        <translation>无法读取保存的布局（%1），已显示默认布局。</translation>
    </message>
    <message>
        <source>Some settings in the saved layout were invalid (%1). Reset to defaults: %2.</source>
        <translation>保存的布局中有设置无效（%1），以下设置已恢复默认值：%2。</translation>
    </message>
    <message>
        <source>The saved layout could not be restored (%1), so the default layout is shown.</source>
        <translation>无法恢复保存的布局（%1），已显示默认布局。</translation>
    </message>
</context>
<context>
    <name>WindowSystem</name>
    <message>
        <source>The configuration file exceeds the 1 MiB limit.</source>
        <translation>配置文件超过 1 MiB 限制。</translation>
    </message>
    <message>
        <source>Invalid JSON at position %1: %2</source>
        <translation>JSON 格式错误（位置 %1）：%2</translation>
    </message>
    <message>
        <source>Cannot create the user configuration directory.</source>
        <translation>无法创建用户配置目录。</translation>
    </message>
    <message>
        <source>settings.json must contain a JSON object.</source>
        <translation>settings.json 必须是 JSON 对象。</translation>
    </message>
    <message>
        <source>Invalid interface font or zoom setting.</source>
        <translation>界面字体或缩放设置无效。</translation>
    </message>
    <message>
        <source>workbench.language supports only system, zh_CN, or en.</source>
        <translation>workbench.language 只支持 system、zh_CN 或 en。</translation>
    </message>
    <message>
        <source>Unsupported display language.</source>
        <translation>不支持此界面语言。</translation>
    </message>
    <message>
        <source>keybindings.json must contain a JSON array with at most 4096 entries.</source>
        <translation>keybindings.json 必须是最多 4096 项的 JSON 数组。</translation>
    </message>
    <message>
        <source>Keybinding rule %1: %2</source>
        <translation>快捷键规则 %1：%2</translation>
    </message>
    <message>
        <source>The rule must be an object.</source>
        <translation>规则必须是对象。</translation>
    </message>
    <message>
        <source>Unsupported field: %1</source>
        <translation>当前不支持字段：%1</translation>
    </message>
    <message>
        <source>command and key must be strings.</source>
        <translation>command 和 key 必须是字符串。</translation>
    </message>
    <message>
        <source>Invalid command ID or shortcut.</source>
        <translation>命令 ID 或快捷键无效。</translation>
    </message>
    <message>
        <source>args must be an object.</source>
        <translation>args 必须是对象。</translation>
    </message>
    <message>
        <source>The keybinding rules exceed command binding limits. Existing bindings were kept.</source>
        <translation>快捷键规则不符合命令绑定限制，已保留原绑定。</translation>
    </message>
    <message>
        <source>The keybinding rules exceed the 4096-entry limit.</source>
        <translation>快捷键规则超过 4096 项限制。</translation>
    </message>
    <message>
        <source>The keybindings file has not been read successfully. The original file was kept.</source>
        <translation>快捷键文件尚未成功读取，未覆盖原文件。</translation>
    </message>
    <message>
        <source>The keybindings file changed externally and was not overwritten. Restart to load the changes.</source>
        <translation>快捷键文件已被外部修改，未覆盖；请重新启动以加载修改。</translation>
    </message>
</context>
</TS>
