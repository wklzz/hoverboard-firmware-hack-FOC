FROM nginx:alpine
# 直接拷贝本地构建好的 dist 静态目录
COPY dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
