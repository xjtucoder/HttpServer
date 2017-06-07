#ifndef HTTP_HTTP_PARSER_H
#define HTTP_HTTP_PARSER_H

#include <string>
#include <vector>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <strings.h>

// #define BUFFER_SIZE 4096

/*
 *  正在解析的请求的状态：
 *  PARSE_REQUESTLINE表示正在解析请求行
 *  PARSE_HEADER表示正在解析首部字段
 */
enum PARSE_STATE { PARSE_REQUESTLINE = 0, PARSE_HEADER };

// 表示正在解析的行的状态，即：读取到一个完整的行、行出错和接收到数据不完整
enum LINE_STATUS { LINE_OK = 0, LINE_ERROR, LINE_MORE };

/*
 * 服务器处理HTTP请求的结果：
 * MORE_DATE表示请求不完整，需继续读取请求
 * GET_REQUEST表示获得了一个完整的客户请求
 * REQUEST_ERROR表示客户请求的语法错误
 * FORBIDDEN_REQUEST表示客户对资源没有访问权限
 * INTERNAL_ERROR表示服务器内部出现错误
 * CLOSE_CONNECTION表示客户端已经关闭连接
 */
enum HTTP_CODE { MORE_DATA = 0, GET_REQUEST, REQUEST_ERROR, FORBIDDEN_REQUEST,
    INTERNAL_ERROR, CLOSE_CONNECTION };

// 客户请求的方法
/* 
 * GET     向特定的资源发出请求
 * POST    向指定资源提交数据进行处理请求
 * HEAD    向服务器索要与GET请求相一致的响应，只不过响应体将不会被返回
 * PUT     向指定资源位置上传其最新内容
 * DELETE  请求服务器删除Request-URI所标识的资源
 * TRACE   回显服务器收到的请求，主要用于测试或诊断
 * OPTIONS 返回服务器针对特定资源所支持的HTTP请求方法
 * CONNECT HTTP/1.1协议中预留给能够将连接改为管道方式的代理服务器
 */
enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE,
    OPTIONS, CONNECT, PATCH };

// 解析请求后的数据存储在http_request结构体中
typedef struct
{
    std::string method;     // 请求的方法
    std::string uri;        // 请求的uri
    std::string version;    // HTTP版本
    std::string host;       // 请求的主机名
    std::string connection; // Connection首部
} http_request;

class http_parser
{
public:
    http_parser(const std::string request);
    ~http_parser();
    http_request get_parse_result();   // 返回解析后的结果
    
private:
    void parse_line();                 // 解析出一行内容
    void parse_requestline();          // 解析请求行
    void parse_headers();              // 解析头部字段

    std::string request;               // 客户请求内容
    std::vector<std::string> lines;    // 存储每一行请求
    http_request parse_result;         // 存储解析结果
};  

#endif  // HTTP_HTTP_PARSER_H
