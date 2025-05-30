# Linux-minimal && myshell 
### How to use? 
```bash
# make help
make               # 编译并打包
make run-nographic # 以无图形化模式运行打包文件
```
> This will compile the project and launch QEMU for simulation. 
> For more details, you can type `make help` or refer to the `Makefile`. 

### How to close? 
- `Ctrl A` + `X` (simulated with QEMU)

### demo show 
![demo](images/demo.png)

> P.S. 现阶段实现了 `mysh -nostdlib` 和 `mysh in cpp` 的两份 shell ，实现了 **环境变量 历史记录 命令解析器 管道 重定向** 等操作，可以在 Makefile 中查看并选择启动项。