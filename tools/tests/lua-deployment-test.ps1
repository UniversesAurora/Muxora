[CmdletBinding()]
param([string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$taskRepository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskOutput = Join-Path $taskRepository 'out'
. (Join-Path $taskRepository 'tools/deployment.ps1')
$taskPackage = if ($PackageDirectory) { [IO.Path]::GetFullPath($PackageDirectory) } else {
    Join-Path $taskOutput 'package/muxora-preview'
}
$taskExecutable = Join-Path $taskPackage 'muxora.exe'
if (!(Test-Path -LiteralPath $taskExecutable)) { throw 'Build the Release deployment before this check.' }
Assert-DeploymentStopped $taskPackage
$taskState = Assert-OutputPath (Join-Path $taskOutput 'lua-deployment-checks') $taskOutput
$taskUser = Join-Path $taskState 'User'
New-Item -ItemType Directory -Path $taskUser -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $taskOutput 'test-results') -Force | Out-Null
$taskReport = Assert-OutputPath (Join-Path $taskOutput 'test-results/lua-deployment.json') $taskOutput
if (Test-Path -LiteralPath $taskReport) { Remove-Item -LiteralPath $taskReport -Force }
$taskInit = Join-Path $taskUser 'init.lua'
$taskLayout = Join-Path $taskState 'window-layout.json'
$taskModules = Assert-OutputPath (Join-Path $taskUser 'lua') $taskOutput
$taskModule = Join-Path $taskModules '部署模块.lua'
$taskUnicodeDirectory = Assert-OutputPath (Join-Path $taskState '中文 文件') $taskOutput
$taskOriginal = Join-Path $taskUnicodeDirectory '原始文件.txt'
$taskRenamed = Join-Path $taskUnicodeDirectory '重命名文件.txt'
$taskOwnedFiles = @($taskInit, $taskLayout, (Join-Path $taskUser 'keybindings.jsonc'),
    (Join-Path $taskUser 'keybindings.json'), (Join-Path $taskUser 'settings.json'),
    $taskModule, $taskOriginal, $taskRenamed)
foreach ($taskFile in $taskOwnedFiles) {
    Assert-OutputPath $taskFile $taskOutput | Out-Null
    if (Test-Path -LiteralPath $taskFile) { Remove-Item -LiteralPath $taskFile -Force }
}
$taskCommon = @'
local startupModule=require('部署模块')
muxora.commands.register({id='user.deployment_check',title='Deployment check'},function(context)
  local state=muxora.toolbars.state(context.window,'user.deployment_tools')
  assert(state and state.visible)
  assert(type(io.open)=='function' and type(io.popen)=='function')
  assert(type(os.execute)=='function' and type(os.rename)=='function')
  assert(type(debug.getinfo)=='function' and type(debug.sethook)=='function')
  assert(type(package.loadlib)=='function' and type(coroutine.resume)=='function')
  assert(type(loadfile)=='function' and type(dofile)=='function')
  assert(type(load)=='function' and type(string.dump)=='function')
  assert(type(collectgarbage('count'))=='number')
  assert(package.cpath=='')
  local module=require('部署模块')
  assert(module==startupModule and module==require('部署模块') and module.value=='模块正文')
  assert(module.window==context.window)
  assert(deployment_module_loads==1)
  assert(os.getenv('MUXORA_LUA_DEPLOYMENT_TEXT')=='中文环境值')
  local directory=assert(os.getenv('MUXORA_LUA_DEPLOYMENT_ROOT'))
  local original=directory..'/原始文件.txt'
  local renamed=directory..'/重命名文件.txt'
  local file=assert(io.open(original,'wb'))
  assert(file:write('Lua 部署正文')); assert(file:close())
  os.remove(renamed)
  assert(os.rename(original,renamed))
  file=assert(io.open(renamed,'rb'))
  assert(file:read('*a')=='Lua 部署正文'); assert(file:close())
  local chunk=assert(load(string.dump(function()return 6*7 end)))
  assert(chunk()==42)
  local child=coroutine.create(function()return muxora.context().window end)
  local ok,window=coroutine.resume(child)
  assert(ok and window==context.window and coroutine.status(child)=='dead')
  local executed,kind,code=os.execute('cmd /d /c exit /b 0')
  assert(executed and kind=='exit' and code==0)
  CHECK_BODY
  assert(muxora.commands.execute('layout.save'))
  assert(os.exit(57,true))
end)
assert(muxora.toolbars.register({id='user.deployment_tools',title='Lua tools',commands={'host.newTab'}}))
assert(muxora.status.register({id='user.deployment_status',text='Lua ready'}))
'@
$taskBodies = @(
    @'
  assert(muxora.windows.set_title(context.window,'Lua deployment saved'))
  assert(muxora.tabs.rename(context.tab,'Lua retained tab'))
  assert(muxora.toolbars.width(context.window,'user.deployment_tools',143))
  assert(muxora.windows.set_visibility(context.window,{tabs=false,sidebar=false}))
'@,
    @'
  assert(state.width==143)
  local visibility=muxora.windows.visibility(context.window)
  assert(visibility.preferred.tabs==false and visibility.preferred.sidebar==false)
  assert(visibility.effective.tabs==false and visibility.effective.sidebar==false)
  local found=false
  for _,tab in ipairs(muxora.tabs.list(context.group)) do
    if tab.title=='Lua retained tab' then found=true end
  end
  assert(found)
  assert(muxora.windows.set_title(context.window,'Lua deployment restored'))
'@
)
$taskPasses = @()
$taskCleanupState = $true
try {
    New-Item -ItemType Directory -Path $taskModules,$taskUnicodeDirectory -Force | Out-Null
    @'
deployment_module_loads=(deployment_module_loads or 0)+1
local context=muxora.context()
assert(type(context.window)=='string')
return {value='模块正文',window=context.window}
'@ | Set-Content -LiteralPath $taskModule -Encoding utf8NoBOM
    for ($taskPass = 0; $taskPass -lt $taskBodies.Count; ++$taskPass) {
        $taskCommon.Replace('CHECK_BODY', $taskBodies[$taskPass]) |
            Set-Content -LiteralPath $taskInit -Encoding utf8NoBOM
        $taskLaunch = [Diagnostics.ProcessStartInfo]::new($taskExecutable)
        $taskLaunch.UseShellExecute = $false
        $taskLaunch.WindowStyle = 'Hidden'
        $taskLaunch.WorkingDirectory = $taskPackage
        $taskLaunch.RedirectStandardError = $true
        $taskLaunch.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
        $taskLaunch.Environment['MUXORA_LUA_DEPLOYMENT_ROOT'] = $taskUnicodeDirectory
        $taskLaunch.Environment['MUXORA_LUA_DEPLOYMENT_TEXT'] = '中文环境值'
        foreach ($taskName in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QML2_IMPORT_PATH')) {
            $taskLaunch.Environment.Remove($taskName) | Out-Null
        }
        if ($taskPass -eq 0) { $taskLaunch.ArgumentList.Add('--fresh') }
        foreach ($taskArgument in @('--state-dir',$taskState,'--command','user.deployment_check')) {
            $taskLaunch.ArgumentList.Add($taskArgument)
        }
        $taskProcess = [Diagnostics.Process]::Start($taskLaunch)
        try {
            $taskErrors = $taskProcess.StandardError.ReadToEndAsync()
            if (!$taskProcess.WaitForExit(20000)) {
                $taskProcess.CloseMainWindow() | Out-Null
                if (!$taskProcess.WaitForExit(10000)) {
                    $taskCleanupState = $false
                    throw "Lua deployment pass $taskPass remains running (PID $($taskProcess.Id)); preserved $taskState."
                }
                throw "Lua deployment pass $taskPass did not exit through its script command."
            }
            $taskDiagnostic = $taskErrors.GetAwaiter().GetResult()
            $taskDiagnostic | Set-Content -LiteralPath (Join-Path $taskOutput "test-results/lua-deployment-$taskPass.log") -Encoding utf8NoBOM
            if ($taskProcess.ExitCode -ne 0) { throw "Lua deployment pass $taskPass failed: $($taskProcess.ExitCode) $taskDiagnostic" }
            $taskPasses += @{ pass = $taskPass; exitCode = $taskProcess.ExitCode }
        } finally { $taskProcess.Dispose() }
        $taskSnapshot = Get-Content -LiteralPath $taskLayout -Raw | ConvertFrom-Json
        $taskExpected = if ($taskPass -eq 0) { 'Lua deployment saved' } else { 'Lua deployment restored' }
        if ($taskSnapshot.windows.Count -ne 1 -or $taskSnapshot.windows[0].title -ne $taskExpected) {
            throw "Lua deployment pass $taskPass did not persist the verified script state."
        }
        if ((Test-Path -LiteralPath $taskOriginal) -or
            [IO.File]::ReadAllText($taskRenamed, [Text.Encoding]::UTF8) -ne 'Lua 部署正文') {
            throw "Lua deployment pass $taskPass did not read, write and rename the Unicode file."
        }
        $taskPasses[-1].title = $taskExpected
    }
    if (!(Test-Path -LiteralPath (Join-Path $taskPackage 'licenses/open-source/lua.txt'))) {
        throw 'The deployed Lua license is missing.'
    }
    @{ sdkRemovedFromPath = $true; scriptStartupAndCommand = $true; toolbarWidthAndVisibilityRestored = $true;
        scriptChromeSavedBeforeUnload = $true; luaLicenseIncluded = $true;
        standardLibraries = $true; standardRequireCached = $true; moduleTopLevelHostCall = $true;
        unicodeFileAndEnvironment = $true;
        coroutineHostCall = $true; binaryChunkAndGarbageCollection = $true;
        standardCommandExecution = $true; osExitSavedNormally = $true; passes = $taskPasses } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'test-results/lua-deployment.json') -Encoding utf8NoBOM
    Write-Host 'Lua deployment passed: standard libraries, Unicode I/O, modules, coroutines, commands, exit, restart, toolbar preferences and license.'
} finally {
    if ($taskCleanupState) {
        foreach ($taskFile in $taskOwnedFiles) {
            Assert-OutputPath $taskFile $taskOutput | Out-Null
            if (Test-Path -LiteralPath $taskFile) { Remove-Item -LiteralPath $taskFile -Force }
        }
        foreach ($taskDirectory in @($taskModules,$taskUnicodeDirectory,$taskUser,$taskState)) {
            Assert-OutputPath $taskDirectory $taskOutput | Out-Null
            if ((Test-Path -LiteralPath $taskDirectory) -and !(Get-ChildItem -LiteralPath $taskDirectory -Force)) {
                Remove-Item -LiteralPath $taskDirectory
            }
        }
    }
}
