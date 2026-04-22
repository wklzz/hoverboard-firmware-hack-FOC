# Hoverboard Admin Backend Deployment

本文档记录了如何编译并部署后端管理系统到远程服务器。

## 1. 服务器信息 (已验证)

- **IP 地址**: `47.99.35.179`
- **SSH 登录**: `ssh -i C:\Users\c\.ssh\nanqiao.pem root@47.99.35.179`
- **Dockge 部署位置**: `/opt/stacks/hoverboard-admin`
- **内网数据库**: `mariadb:3306` (处于 `nanqiao` 容器网络中)

## 2. 本地编译

在 Windows 本地环境下编译为 Linux 可执行文件：

```powershell
cd admin-go
$env:GOOS="linux"
$env:GOARCH="amd64"
go build -o main main.go
```

## 3. 同步到服务器

使用 `scp` 将后端二进制文件和配置文件同步到服务器。

```powershell
# 同步二进制文件、Dockerfile 和配置文件夹
scp -i C:\Users\c\.ssh\nanqiao.pem main Dockerfile root@47.99.35.179:/opt/stacks/hoverboard-admin/admin-go/
scp -r -i C:\Users\c\.ssh\nanqiao.pem config/ root@47.99.35.179:/opt/stacks/hoverboard-admin/admin-go/
```

## 4. 在服务器上更新服务

登录服务器并重启后端容器：

```bash
ssh -i C:\Users\c\.ssh\nanqiao.pem root@47.99.35.179
cd /opt/stacks/hoverboard-admin

# 强制重新构建后端镜像并启动
docker compose up -d --build backend
```

## 5. 注意事项

- **数据库连接**：代码已修改为通过内网地址 `mariadb:3306` 连接，不再走公网。这要求后端容器必须加入 `nanqiao` 网络（已在 `docker-compose.yaml` 中配置）。
