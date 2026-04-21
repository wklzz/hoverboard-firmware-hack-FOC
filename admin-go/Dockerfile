FROM alpine:latest
WORKDIR /app
# 直接拷贝本地编译好的 Linux 二进制文件
COPY main .
COPY config ./config
RUN chmod +x main
EXPOSE 8080
CMD ["./main"]
