/*
 * @Author: fs1n
 * @Email: fs1n@qq.com
 * @Description: {To be filled in}
 * @Date: 2023-03-22 16:05:55
 * @LastEditors: fs1n
 * @LastEditTime: 2023-03-22 17:26:06
 */
#include "../include/http_conn.h"

void test_parse_request_line(){
    char text[1024];
    memset(text, '\0', sizeof(text));
    strcpy(text, "GET /index.html HTTP/1.1\0");
    http_conn *hc = new http_conn();
    hc->init();
    std::cout << "text: " << text << std::endl;
    hc->parse_request_line(text);
}

void test_parse_header(){
    char text[1024];
    memset(text, '\0', sizeof(text));
    strcpy(text, "Content-Length:1024");
    http_conn *hc = new http_conn();
    hc->init();
    std::cout << "text: " << text << std::endl;
    hc->parse_header(text);
}

int main(){
    // test_parse_request_line();
    // test_parse_header();

    return 0;
}