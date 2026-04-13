# Example

所有文件如下
```bash
.
├── host.c
├── host.h
├── inc
│   ├── ht.h
│   └── list.h
├── introspect.dtd
├── introspect.xml
├── introspect.xml.h
├── shared.c
├── shared.h
├── test
│   ├── Makefile
│   ├── single.c
│   ├── snh.c
│   └── snw.c
├── watcherd.c
└── watcherd.h
```
`.dtd` 文件是验证xml格式的文件

将 `.xml` 文件转换为 `.h` 文件使用
```bash
xxd -i introspect.xml introspect.xml.h
```
测试使用

```bash
cd test
make all
```

`single` 是单文件程序, 可以完成 `watcher` 和 `host` 的任务, `snw` 和 `snh` 分别只包含 `watcher` 和 `host` 部分

对于执行 `host` 功能的部分, 使用 `register_sni_handler(f_sni_handler);` 注册回调, 使用 `handle_snis()` 对获取到的所有 `StatusNotifierItem` 对象进行处理

