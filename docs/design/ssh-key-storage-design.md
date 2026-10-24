# SSH 私钥存放与 Muxora 代理设计

状态：设计稿，2026-10-24，用户确认方向。补充 [V0 设计 §15](v0-design.md#15-设置凭据与持久化) 的凭据规则，并与 [SSH config 修改与同步](ssh-config-sync-design.md) 配合。实现步骤见 [阶段 7.13、7.14](../roadmap/implementation/phase-7-terminal-and-sessions.md#713-私钥存放与密钥管理)。

## 1. 同类软件的做法

同类软件大致分两派：

- **只记文件路径**：SecureCRT 生成的密钥默认放在 `%APPDATA%\VanDyke`，会话也可以指定任意密钥文件（[VanDyke](https://vandyke.com/support/tips/publickeyauth.html)）；PuTTY、MobaXterm、VS Code Remote-SSH 和 `ssh` 同样按路径使用密钥。好处是 `ssh`、git、VS Code 等工具能共用同一把密钥。
- **放进自己的保管库**：Xshell 把密钥用自己的格式存在用户文档目录下，导出后才能给别的程序用（[NetSarang](https://netsarang.atlassian.net/wiki/x/OoEWFg)）；Termius 放进加密保管库，可以同步或只留在本机（[Termius](https://docs.termius.com/security/encryption-overview)）；Tabby 加密后存在自己的配置文件里（[Tabby](https://github.com/Eugeny/tabby/wiki/Vault-Encryption)）。好处是跟着软件统一管理，缺点是外部工具用不了。

1Password、Bitwarden 这类密码管理器用自己的 SSH 代理解决这个缺点：私钥留在保管库里，外部 `ssh` 通过代理请求签名。在 Windows 上，它们接管 OpenSSH 的标准管道 `\\.\pipe\openssh-ssh-agent`，所以要求先停用系统自带的代理服务，同一时间也只能有一个程序持有这个管道（[1Password 社区](https://www.1password.community/developers-69/feature-request-disable-windows-ssh-agent-service-when-enabling-1pass-ssh-agent-20298)）。Muxora 两种存放方式都提供，并按这种方式提供代理（§5）。

## 2. 设置

| 设置 | 默认 | 说明 |
| --- | --- | --- |
| `ssh.keys.storage` | `sshDirectory` | Muxora 新生成或新导入的私钥放在哪里：`sshDirectory`（`~/.ssh`）或 `vault`（Muxora 保管库） |
| `ssh.agent.enabled` | `false` | 运行 Muxora 代理，让外部工具使用保管库里的私钥 |
| `ssh.agent.start` | `login` | 代理什么时候启动：`login`（登录 Windows 时）或 `onDemand`（外部 `ssh` 用到时才启动，见 §5.2） |

三个设置回答三个不同的问题：钥匙放哪里、外部工具用不用保管库的钥匙、代理什么时候运行。会话要不要给外部工具用，由 `ssh.config.syncAppSessions` 决定。只有“钥匙在保管库、会话又要同步给外部工具”这一种组合需要代理：

| 私钥位置 | 同步 config | 代理 | 同步出去的块里写什么 | 外部工具能否登录 |
| --- | --- | --- | --- | --- |
| `~/.ssh` | 关 | 不需要 | 不同步 | 只有 Muxora 用 |
| `~/.ssh` | 开 | 不需要 | `IdentityFile <私钥路径>` | 能 |
| 保管库 | 关 | 不需要 | 不同步 | 只有 Muxora 用 |
| 保管库 | 开 | 开 | `IdentityAgent <代理管道>`、`IdentityFile <导出的公钥>`、`IdentitiesOnly yes` | 能 |
| 保管库 | 开 | 关 | 不写密钥，块里加一行注释说明原因 | 不能，界面提示（§6） |

## 3. 两种存放位置

会话只保存引用：`~/.ssh` 里的密钥记文件路径，保管库里的密钥记密钥 ID。私钥内容从不写进会话文件、布局、设置、任务记录或日志。

**`~/.ssh`**：写成标准 OpenSSH 格式的私钥和 `.pub` 文件，文件权限只给当前用户（OpenSSH 拒绝权限过宽的私钥）。不覆盖已有的同名文件，重名时让用户另取名字。

**保管库**：每把私钥一个文件，放在 `User/keys/<密钥 ID>`，内容用系统账户保护：Windows 上用 DPAPI 按当前用户加密，macOS、Linux 以后用各自的钥匙串。不设主口令（用户 2026-10-24 决定）：登录了系统就能用，代理也因此不需要界面就能读取（§5）。名称、类型、指纹和公钥不是秘密，和私钥文件放在一起，便于列表和导出。私钥文件不放进 Windows 凭据管理器，因为它单条能存的内容太小，放不下较长的 RSA 私钥。

**口令**：私钥本身的口令沿用 V0 设计：默认只在本次使用，用户选择记住时进入系统凭据库。

## 4. 切换存放位置

修改 `ssh.keys.storage` 时，如果另一处有 Muxora 管理的密钥（Muxora 生成、导入或被应用会话引用的），弹出对话框（用户 2026-10-24 决定）：

> 有 3 个 Muxora 管理的密钥在 `~/.ssh`。要移到保管库吗？
> ☑ 在原位置保留一份
> [迁移]　[不迁移]

- 迁移后更新会话里的引用和同步出去的 config。
- 用户自己的 SSH config 引用的私钥文件永远不删，没勾选“保留”时也一样；对话框列出这些文件和原因。
- 不迁移时，已有密钥留在原处继续可用，新密钥放到新位置。
- 迁移失败的密钥逐个报告，原文件和引用不变。

## 5. Muxora 代理

### 5.1 独立的小程序

代理是单独的可执行程序 `muxora-agent`，不带窗口、托盘或其他界面，只读取保管库并替外部工具签名。它只依赖 Qt Core、管道和签名所需的库，不加载界面和 Lua。主程序和代理共用同一份保管库读写代码。

- 主程序自己的连接直接读取保管库，不经过代理。
- 保管库变化时代理重新读取，不需要重启。
- 同一用户同一时间只运行一个代理；设置页显示代理是否在运行，并提供“启动”按钮。
- 关闭 `ssh.agent.enabled` 时代理退出，删除它的启动项。

### 5.2 启动方式

- `login`（默认，与 1Password 相同）：写入当前用户的登录启动项（`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`），不需要管理员权限。主程序启动时代理不在运行，也会把它拉起来。
- `onDemand`：不常驻。同步出去的块前面加一个 `Match host <别名> exec "<muxora-agent> --ensure-running"`，`ssh` 解析配置时先运行它：代理已在运行就立即返回，否则启动代理、等它就绪再返回。只对用了保管库密钥的同步会话生效。

### 5.3 管道

默认使用 Muxora 自己的命名管道，名称带当前用户的标识，避免多个 Windows 用户同时登录时冲突，访问权限只给当前用户。同步出去的块用 `IdentityAgent` 指向它，所以只影响这些会话，不碰系统自带的代理和用户自己的 config。

### 5.4 与其他代理共存

用户可能同时开着 1Password、Bitwarden 或系统自带的代理（用户 2026-10-24 提出，自己就在用 1Password 的代理）。它们占用标准管道，Muxora 代理用自己的管道，所以可以同时运行：

- 只有同步出去、用了保管库密钥的主机指向 Muxora 代理，其他主机照旧使用标准管道上的代理。OpenSSH 一次连接只问一个代理，所以指向 Muxora 代理的主机不会再用其他代理里的密钥；要用其他代理里的密钥，这个会话就不要选保管库密钥。
- Muxora 自己的连接支持代理登录，使用标准管道上的代理，也就是用户已有的 1Password 等。密钥已经放在这类工具里的用户，可以完全不用保管库。
- 如果 §7 的验证表明只能接管标准管道，开启代理前先检查管道是否已被占用：被占用时不抢，说明是哪个程序占着，并建议把密钥放进那个工具或导出到 `~/.ssh`。

### 5.5 使用记录

代理每次替外部程序签名时，发一条 Windows 系统通知（例如“ssh 使用了密钥 dev-key”），并记一条有上限的使用记录（放 `runtime.json`），主程序的密钥管理页可以查看。不逐次弹窗确认。

## 6. 界面

- **设置页（SSH › 密钥）**：“新密钥放在”下拉框；“让外部工具通过 Muxora 使用保管库里的密钥”复选框；代理启动方式；代理状态和“启动”按钮。
- **迁移对话框**：见 §4。
- **密钥管理页**（参考 Xshell 的用户密钥管理）：列出两处的全部密钥（名称、类型、指纹、位置、被哪些会话使用、最近被外部程序使用的时间）；操作：新建、导入、导出到 `~/.ssh`、移到保管库、复制公钥、删除（被会话引用时先列出这些会话）。
- **同步会话的提示**：同步会话用的是保管库里的密钥、代理又没开时，会话设置里显示“外部工具无法使用这把密钥”，并给出“开启 Muxora 代理”和“导出到 ~/.ssh”两个操作。

## 7. 开工前要先验证

- Windows 自带的 OpenSSH 是否接受 `IdentityAgent` 指向自定义命名管道，以及管道路径在 config 里的写法（反斜杠是转义字符，可能要写成 `//./pipe/<名称>`）。JGit 支持这种写法，Win32-OpenSSH 本身还需要确认。不支持时，退回 1Password 的做法：接管标准管道，并在开启代理时说明需要停用系统代理服务。
- Windows 上的 `Match exec` 能否可靠地启动代理并等待就绪，不行时 `onDemand` 不提供。
- 代理对 ed25519、ECDSA 和 RSA（`rsa-sha2-256`／`rsa-sha2-512`）签名的结果能被真实的 `ssh` 接受。

## 8. 以后再做

- 把保管库里的密钥加进系统自带的 `ssh-agent`：不需要额外进程，但该服务默认停用、开启要管理员权限，而且会在保管库外多一份副本，删除时要两边一起删。
- macOS、Linux 的保管库保护方式和代理接入。
