# LuaRuntime

Qt Core 与官方 Lua 5.5.1 的嵌入运行时。它不依赖窗口、终端、SSH 或 Muxora 配置。

链接 `LuaRuntime::Runtime`，在宿主线程创建 `ScriptRuntime`。`execute` 和 `invoke` 返回非零任务 ID 时，结果通过 `finished` 异步返回；UI 线程不等待解释器。校验失败或任务队列已满时，使用另一条有界错误回执队列，其容量同样为 `maximumPendingJobs`。错误回执队列也已满时返回 `0`，表示请求未被接纳，而且不会再投递该请求的 `finished`；宿主必须处理这个返回值。`HostHandler` 在宿主线程执行，Lua 协程在等待回执时让出，恢复前核验任务、取消和 VM 代次。

默认上限包括 32 MiB Lua 分配、128 个待处理任务、128 个待投递错误回执、8 MiB 传输保留数据、1024 个回调及每任务指令／时间预算。`memoryBytes`、`peakMemoryBytes`、`pendingJobs`、`pendingRejections`、`retainedCallbacks` 提供实际计数。两条队列在回执发出前释放对应名额；排空后可以继续提交。Lua 的内存预算不包含 Qt、宿主 UI 或系统运行库。

`reset` 取消任务并关闭完整 VM；旧回调 ID 和旧回执不能作用于新 VM。`releaseCallback` 显式释放注册函数。日志、字符串、对象深度、节点、源文件和待投递结果也有上限。错误回传有界诊断和堆栈，不向 GUI 抛出 Lua 异常。

Lua 标准库中的本地算法不能被指令 hook 强制抢占，这不是恶意代码的进程隔离。垃圾回收钩子的嵌入适配见 [依赖说明](../../third_party/lua/README.md)。实际测试结果见项目预览记录，不能把存在测试入口当作已经通过。
