# http请求处理类
处理客户端请求的主要逻辑，包含解析http请求，生成http响应，处理客户端消息，调用API生成聊天回复。程序运行会创建作业池，随机分配一个空闲作业实例给当前客户，执行完当前作业后清空数据，返回作业池进入休眠状态。

- 读取客户端的http请求
- 状态机解析报文
- 根据请求内容取指定资源
- 生成http响应报文
