# Todo: Docker / Compose 交付骨架

## 1. 第一刀：文件骨架
- [x] 先固定 Docker 目录结构：`docker/` + 根目录 `compose.yaml`
- [x] 创建后端镜像骨架：`docker/Dockerfile.server`
- [x] 创建 AI proxy 镜像骨架：`docker/Dockerfile.ai-proxy`
- [x] 创建 Compose 骨架：`compose.yaml`
- [x] 在骨架文件里补中文注释，先把职责边界写清楚

## 2. 第二刀：后端镜像
- [ ] 补前端构建 stage
- [x] 补 C++ 后端构建 stage
- [x] 补运行时 stage
- [x] 明确 SQLite 数据目录与 `client/dist` 放置位置

## 3. 第三刀：AI proxy 镜像
- [x] 补 Python 依赖安装
- [x] 补 proxy 代码拷贝
- [x] 补默认启动命令

## 4. 第四刀：Compose 联动
- [x] 让后端通过服务名访问 AI proxy，而不是写死 `127.0.0.1`
- [x] 补端口映射
- [x] 补数据卷
- [x] 补 ai-proxy service 骨架与 server 的最小 `depends_on`
- [ ] 补最小环境变量与启动参数

## 5. 第五刀：验证
- [ ] `docker compose config`
- [ ] `docker compose build`
- [ ] `docker compose up`
- [ ] 浏览器访问单入口页面
