# Pico UI Layout Editor

本地拖拽式固件页面布局编辑器。用于先把 640×360 逻辑坐标排好，再同步到游戏模块或固件绘制代码。

## 启动

```bash
python3 tools/ui-layout-editor/server.py --port 8787
```

打开：

```text
http://127.0.0.1:8787
```

当前示例截图见 `editor-preview.png`。编辑后约 700ms 自动保存到 `layouts/*.json`。

## 已内置页面

- 贪吃蛇 · 单机横屏
- 2048 · 单机横屏
- 贪吃蛇 · 副机手柄
- 贪吃蛇 · 主机棋盘
- 五子棋 · 单机横屏

底图来自当前固件渲染，元素坐标为逻辑像素，实际绘制时会乘 1.9 到 1216×684。

## 操作

- 拖动元素：移动位置，默认吸附 8px 网格，按住 Shift 可自由移动
- 拖动右下角：改变大小
- 方向键：移动 1px；Shift + 方向键：10px
- Delete：删除
- Cmd/Ctrl+Z：撤销；Cmd/Ctrl+Shift+Z：重做
- 「导出 C」：生成坐标表和 `ps_button/ps_text` 调用示例

## 保存

编辑后约 700ms 自动保存到：

```text
tools/ui-layout-editor/layouts/<page>.json
```

保存接口是本地 Python 服务，只写这个 `layouts` 目录。

## 后续接入固件

1. 在网页里拖动到满意位置
2. 导出 C，或直接查看 `layouts/*.json`
3. 把坐标同步到对应的 `games/src/builtin_games.c` 页面函数
4. 重新构建 `.pga` 游戏包并写入 TF 卡

如果是要改真机固件内的系统页面，则把同一组坐标接入 `main/system` 的绘制函数后重新编译固件。
