#include "parse_arg.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string password = "123456";
    string databasename = "user_profile";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.PORT, user, password, databasename, 
    config.LOGWrite, config.TRIGMode, config.thread_num, config.close_log);
    

    //日志
    server.logwrite();

    //数据库
    server.dbConn();

    //线程池
    server.threadpool_create();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}