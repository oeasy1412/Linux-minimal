FROM ubuntu:jammy

ARG TAR_FILE=myminilinux-build-artifacts

ENV TZ=Asia/Shanghai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# 设置工作目录
WORKDIR /app

# 将本地的脚本复制到工作目录
COPY tools/run-qemu.sh /app/

# 将构建产物复制进镜像 (这个文件名要和 workflow 里解压/下载的文件名一致)
COPY ${TAR_FILE} /app/

# 设置sudo免密码
RUN apt update && \
    apt install --no-install-recommends -y git ca-certificates curl gnupg wget sudo apt-utils tar qemu-system-x86 && \
    git config --global --add safe.directory '*' && \
    sudo apt autoremove -q -y && \
    sudo apt clean -q -y

# 解压内核跟磁盘镜像
RUN tar -xvf ${TAR_FILE} && rm ${TAR_FILE}

# 设置容器启动时的默认命令
CMD ["/bin/bash"]