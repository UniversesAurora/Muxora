<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>LuaRuntime</name>
    <message>
        <source>No Lua host handler is installed</source>
        <translation>未安装 Lua 宿主处理器</translation>
    </message>
    <message>
        <source>Lua host handler raised an unknown exception</source>
        <translation>Lua 宿主处理器发生未知异常</translation>
    </message>
    <message>
        <source>Lua task queue is full</source>
        <translation>Lua 任务队列已满</translation>
    </message>
    <message>
        <source>Lua task queue exceeds its byte limit</source>
        <translation>Lua 任务队列超过字节限制</translation>
    </message>
    <message>
        <source>Lua source exceeds its configured byte limit</source>
        <translation>Lua 源码超过配置的字节限制</translation>
    </message>
    <message>
        <source>Lua source name is too long</source>
        <translation>Lua 源码名称过长</translation>
    </message>
    <message>
        <source>Invalid Lua callback ID</source>
        <translation>Lua 回调 ID 无效</translation>
    </message>
    <message>
        <source>Lua host operation timed out</source>
        <translation>Lua 宿主操作超时</translation>
    </message>
    <message>
        <source>Lua task was cancelled</source>
        <translation>Lua 任务已取消</translation>
    </message>
    <message>
        <source>Unable to allocate the Lua interpreter</source>
        <translation>无法为 Lua 解释器分配内存</translation>
    </message>
    <message>
        <source>Lua callback no longer exists</source>
        <translation>Lua 回调已不存在</translation>
    </message>
    <message>
        <source>Unexpected Lua coroutine suspension</source>
        <translation>Lua 协程意外挂起</translation>
    </message>
    <message>
        <source>Host operations require a yieldable Lua task</source>
        <translation>宿主操作需要可让出的 Lua 任务</translation>
    </message>
    <message>
        <source>Host method cannot be empty</source>
        <translation>宿主方法不能为空</translation>
    </message>
    <message>
        <source>Lua task no longer exists</source>
        <translation>Lua 任务已不存在</translation>
    </message>
    <message>
        <source>Lua stack has no space for host results</source>
        <translation>Lua 栈没有足够空间接收宿主结果</translation>
    </message>
    <message>
        <source>Cannot register a Lua callback during cleanup</source>
        <translation>清理期间不能注册 Lua 回调</translation>
    </message>
    <message>
        <source>Callback must be a Lua function</source>
        <translation>回调必须是 Lua 函数</translation>
    </message>
    <message>
        <source>Lua callback count exceeds its configured limit</source>
        <translation>Lua 回调数量超过配置的上限</translation>
    </message>
    <message>
        <source>Callback ID must be a string</source>
        <translation>回调 ID 必须是字符串</translation>
    </message>
    <message>
        <source>Invalid callback ID</source>
        <translation>回调 ID 无效</translation>
    </message>
    <message>
        <source>Lua allocator could not allocate memory</source>
        <translation>Lua 分配器无法分配内存</translation>
    </message>
    <message>
        <source>Lua task failed with a non-string error</source>
        <translation>Lua 任务失败，错误值不是字符串</translation>
    </message>
    <message>
        <source>Lua result exceeds the pending transfer byte limit</source>
        <translation>Lua 结果超过待传输数据的字节限制</translation>
    </message>
    <message>
        <source>Lua value exceeds the configured depth or node limit</source>
        <translation>Lua 值超过配置的深度或节点数量限制</translation>
    </message>
    <message>
        <source>Lua value exceeds the configured transfer byte limit</source>
        <translation>Lua 值超过配置的传输字节限制</translation>
    </message>
    <message>
        <source>Lua string exceeds the configured byte limit</source>
        <translation>Lua 字符串超过配置的字节限制</translation>
    </message>
    <message>
        <source>Lua integer is outside the signed 64-bit range</source>
        <translation>Lua 整数超出 64 位有符号整数范围</translation>
    </message>
    <message>
        <source>Lua values cannot contain NaN or infinity</source>
        <translation>Lua 值不能包含 NaN 或无穷大</translation>
    </message>
    <message>
        <source>Only nil, JSON null, booleans, finite numbers, UTF-8 strings, lists and maps cross the Lua boundary</source>
        <translation>Lua 与宿主之间只能传递 nil、JSON null、布尔值、有限数值、UTF-8 字符串、列表和映射</translation>
    </message>
    <message>
        <source>Lua stack has no space for the supplied value</source>
        <translation>Lua 栈没有足够空间接收提供的值</translation>
    </message>
    <message>
        <source>Unsupported host value type in Lua arguments</source>
        <translation>Lua 参数包含不支持的宿主值类型</translation>
    </message>
    <message>
        <source>Lua argument must be a string</source>
        <translation>Lua 参数必须是字符串</translation>
    </message>
    <message>
        <source>Lua string is too large</source>
        <translation>Lua 字符串过大</translation>
    </message>
    <message>
        <source>Lua strings crossing the host boundary must contain valid UTF-8</source>
        <translation>Lua 与宿主之间传递的字符串必须是有效的 UTF-8</translation>
    </message>
    <message>
        <source>Lua functions, threads and userdata cannot cross the host boundary; register callbacks explicitly</source>
        <translation>Lua 函数、线程和 userdata 不能传给宿主；请显式注册回调</translation>
    </message>
    <message>
        <source>Cyclic Lua tables cannot cross the host boundary</source>
        <translation>包含循环引用的 Lua 表不能传给宿主</translation>
    </message>
    <message>
        <source>Lua stack has no space for reading a table</source>
        <translation>Lua 栈没有足够空间读取表</translation>
    </message>
    <message>
        <source>Mixed Lua map/list tables cannot cross the host boundary</source>
        <translation>混合映射与列表的 Lua 表不能传给宿主</translation>
    </message>
    <message>
        <source>Lua lists must use contiguous positive integer keys</source>
        <translation>Lua 列表必须使用连续的正整数键</translation>
    </message>
    <message>
        <source>Lua map keys must be strings; list keys must be contiguous integers</source>
        <translation>Lua 映射的键必须是字符串，列表的键必须是连续整数</translation>
    </message>
    <message>
        <source>Sparse Lua lists cannot cross the host boundary</source>
        <translation>稀疏 Lua 列表不能传给宿主</translation>
    </message>
</context>
</TS>
