# LuaRuntime

Qt Core 与官方 Lua 5.5.1 的嵌入运行时。它不依赖窗口、终端、SSH 或 Muxora 配置。

链接 `LuaRuntime::Runtime`，在宿主线程创建 `ScriptRuntime`。`execute` 和 `invoke` 返回非零任务 ID 时，结果通过 `finished` 异步返回；UI 线程不等待解释器。校验失败或任务队列已满时，使用另一条有界错误回执队列，其容量同样为 `maximumPendingJobs`。错误回执队列也已满时返回 `0`，表示请求未被接纳，而且不会再投递该请求的 `finished`；宿主必须处理这个返回值。`HostHandler` 在宿主线程执行，Lua 协程在等待回执时让出，恢复前核验任务、取消和 VM 代次。

本地脚本使用完整 Lua 标准库，没有时间、指令数或 Lua 堆内存预算。`cancelAll` 请求停止当前和已排队的任务；正常运行只保留计数检查和用户请求的钩子事件，停止后才加密检查，保护调用、嵌套协程和用户 debug 钩子不能吞掉停止请求。保留的用户协程在新任务中恢复后回到正常检查频率。用户协程内的宿主调用逐层转发私有让出标记，普通 `coroutine.yield` 仍保持标准的参数和返回值。`debug.sethook`／`gethook` 保存用户钩子的函数、事件和计数，不移除运行时的停止检查。

宿主传输仍有边界，防止界面线程被队列和大量数据占满；默认上限及理由见 [Lua 使用说明](../../docs/development/lua-guide.md#队列传输与资源计数)，独立宿主可通过 `RuntimeLimits` 调整。这些边界不限制脚本内部的字符串、表、标准 `load` 或模块加载。`memoryBytes`、`peakMemoryBytes`、`pendingJobs`、`pendingRejections`、`retainedCallbacks` 提供实际计数。两条任务队列在回执发出前释放对应名额；排空后可以继续提交。

传输保留 JSON `null` 与 Lua `nil` 的区别：显式空值在 Lua 中为不可变哨兵，可原样放回列表或对象；不会删除字段、缩短数组。无效 `QVariant` 仍对应 `nil`；其他 userdata、指针和稀疏／混合表继续拒绝。

`reset` 取消任务并关闭完整 VM；旧回调 ID 和旧回执不能作用于新 VM。`resetFinished` 只通知投递时仍为最新代次的重置；被后续重置取代的完成消息会被丢弃，不能把每次 `reset()` 都当作必有一次完成通知。宿主可用它启动最新一代，避免延迟消息重复触发初始化。`releaseCallback` 显式释放注册函数。错误回传有界诊断和堆栈，不向 GUI 抛出 Lua 异常。

`isRunning`／`runningChanged` 描述正在执行 Lua 或同步标准库调用，等待宿主回复不算执行时间。连续执行两秒后 `isLongRunning`／`longRunningChanged` 提示宿主显示停止入口，不会中止脚本。重置两秒仍未完成时发出 `resetBlocked`，原重置在阻塞调用返回后继续完成。析构最多等待工作线程 1.9 秒；仍在标准库调用中时，线程、解释器及分配统计继续存活，调用返回后自行清理。不会强杀线程，也不会销毁运行中的 `QThread`。

Lua 标准库中的本地算法不能被指令 hook 强制抢占，这不是恶意代码的进程隔离。Lua 按 C++ 静态链接，不支持外部 C 模块；`package.cpath` 为空，`package.loadlib` 保留。垃圾回收钩子的嵌入适配见 [依赖说明](../../third_party/lua/README.md)。实际测试结果见项目预览记录，不能把存在测试入口当作已经通过。
