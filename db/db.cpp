#include "db.h"

MyDB::MyDB()
{
    connection = mysql_init(nullptr);   //初始化数据库连接变量
    if(connection == nullptr)
    {
        std::cout<<"mysql_init error!"<<std::endl;
        exit(1);
    }
}

MyDB::~MyDB()
{
    if(connection != nullptr)
    {
        mysql_close(connection);
    }
}

bool MyDB::initDB(std::string host,std::string user,std::string pwd,std::string db_name)
{
    // 函数mysql_real_connect建立一个数据库连接
	// 成功返回MYSQL*连接句柄，失败返回NULL
    if ((connection = mysql_real_connect(connection, host.c_str(), user.c_str(), pwd.c_str(), db_name.c_str(), 3306, nullptr, 0)) == nullptr) {
        std::cout << "sql connect error" << std::endl;
    }
    // char exesql[128] = "insert into users (username, password) values ('RuiX', '12345')";
    // if (mysql_query(connection, exesql)!=0) {
    //     std::cout << "sql exe error" << std::endl;
    // }
    if(connection == nullptr)
    {
        std::cout<<"mysql_real_connect error!"<<std::endl;
        exit(-1);
        return false;
    }
    return true;
}

bool MyDB::exeSQL(std::string sql)
{
    // mysql_query()执行成功返回0，失败返回非0值.
    if(mysql_query(connection, sql.c_str()) != 0)
    {
        std::cout<<"mysql_query error!"<<std::endl;
        return false;
    }
    if (sql.substr(0, 6) == "SELECT" || sql.substr(0, 6) == "select")
    {
        result = mysql_store_result(connection);  //获取结果集
        // mysql_field_count()返回connection查询的列数
        // while ((row = mysql_fetch_row(result)) != nullptr)
        // {
        //     // mysql_num_fields()返回结果集中的字段数
        //     for(int j = 0;j < mysql_num_fields(result);++j)
        //     {
        //         std::cout<<row[j]<<" ";
        //     }
        //     std::cout<<std::endl; 
        // }
        // 释放结果集的内存
        if (mysql_num_rows(result) > 0) {
            std::cout << "find user info, rows: " << mysql_num_rows(result) << std::endl;
            return true;
        }
        else return false; // 没有查到信息
        mysql_free_result(result);
    }
    return true;
}
