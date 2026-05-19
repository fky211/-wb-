# 新能源充电桩实时监控系统

基于C语言 + epoll + SQLite3的轻量级充电桩状态监控Web服务器。

## 项目结构

```
wbserver_demo1/
├── src/
│   ├── main.c            # 主程序入口、路由处理
│   ├── http_server.c/h   # epoll HTTP服务器核心
│   ├── sqlite_db.c/h     # SQLite3数据库操作
│   ├── json_utils.c/h    # JSON序列化工具
│   └── logger.c/h        # 日志系统
├── www/
│   ├── index.html        # 前端监控页面
│   └── login.html        # 登录注册页面
├── logs/                 # 日志目录（自动创建）
├── Makefile              # 编译脚本
└── README.md
```

## 编译环境要求

- Linux系统 (epoll是Linux特有API)
- GCC编译器
- SQLite3开发库
- pthread库

## 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install -y libsqlite3-dev gcc make

# CentOS/RHEL
sudo yum install -y sqlite-devel gcc make
```

## 编译运行

```bash
# 编译
make

# 运行 (默认端口8080)
./charger_server

# 指定端口
./charger_server 9090

# 调试版本
make debug
```

## 日志系统

日志自动输出到控制台和文件 `logs/server.log`，支持以下级别：

| 级别 | 颜色 | 说明 |
|------|------|------|
| DEBUG | 青色 | 调试信息 |
| INFO | 绿色 | 普通信息 |
| WARN | 黄色 | 警告信息 |
| ERROR | 红色 | 错误信息 |
| FATAL | 紫色 | 致命错误 |

日志文件自动滚动，默认单个文件最大10MB，超过后自动备份为 `.log.1`。

## 默认账号

系统初始化时自动创建默认管理员账号：
- 用户名: `admin`
- 密码: `admin123`

## API接口

### 充电桩接口

| 接口 | 方法 | 说明 | 参数 |
|------|------|------|------|
| `/api/chargers` | GET | 获取所有充电桩 | `area=朝阳区` 可选 |
| `/api/chargers/idle` | GET | 获取空闲充电桩 | - |
| `/api/stats` | GET | 获取统计信息 | - |
| `/api/update` | GET | 更新充电桩状态 | `id=1&status=0` |

状态值: 0-空闲, 1-占用, 2-故障

### 用户认证接口

| 接口 | 方法 | 说明 | 参数 |
|------|------|------|------|
| `/api/user/register` | GET | 用户注册 | `username=xxx&password=xxx&nickname=xxx` |
| `/api/user/login` | GET | 用户登录 | `username=xxx&password=xxx` |
| `/api/user/logout` | GET | 用户登出 | - |
| `/api/user/info` | GET | 获取当前用户信息 | - |

登录成功后会设置HttpOnly Cookie，后续请求自动携带认证信息。

## 访问前端

启动服务器后，浏览器访问: http://localhost:8080

未登录会自动跳转到登录页面。

## Windows环境编译

此项目使用Linux epoll API，无法直接在Windows编译。建议:

1. **WSL (推荐)**: Windows Subsystem for Linux
   ```bash
   wsl
   sudo apt-get install -y libsqlite3-dev gcc make
   make
   ./charger_server
   ```

2. **Docker**:
   ```bash
   docker run -it -v $(pwd):/project -w /project ubuntu:22.04 bash
   apt-get update && apt-get install -y libsqlite3-dev gcc make
   make
   ```

3. **虚拟机**: VMware/VirtualBox安装Ubuntu
