#ifndef DB_H
#define DB_H
#include<iostream>
#include <cstring>
#include<mysql/mysql.h>

class MyDB
{
public:
    MyDB();
    ~MyDB();
    bool initDB(std::string host,std::string user,std::string pwd,std::string db_name);
    bool exeSQL(std::string sql);   
    
private:
    MYSQL*connection;//连接mysql句柄指针
    MYSQL_RES*result;//指向查询结果的指针
    MYSQL_ROW row;	 //按行返回的查询信息
};

#endif