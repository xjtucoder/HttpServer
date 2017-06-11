#include <iostream>
#include <cassert>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "http_parser.h"
#include "http_process.h"
#include "threadpool.h"

#define MAX_EVENTS 10000

// 将描述符fd设置为非阻塞
int setnonblocking(const int fd)
{
    int old_options = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old_options | O_NONBLOCK);
    return old_options;
}

/*
 * 将套接字sockfd添加到epoll监听列表中
 * is_one_shot用于选择是否开启EPOLLONESHOT选项
 */
void add_sockfd(const int epollfd, const int sockfd, const bool is_one_shot)
{
    epoll_event event;
    event.data.fd = sockfd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(is_one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event);
    setnonblocking(sockfd);
}

// 将sockfd从epoll监听列表中移除
void rm_sockfd(const int epollfd, const int sockfd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
    close(sockfd);
}

// 改变在sockfd上监听的事件
void modfd(const int epollfd, const int sockfd, const int ev)
{
    epoll_event event;
    event.data.fd = sockfd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
}

// 注册信号signo的信号处理函数
void addsig(const int signo, void (handler)(int), bool is_restart = true)
{
    struct sigaction sa;

    //将sa中的所有字节替换为0
    memset(&sa, 0, sizeof(sa));

    //信号处理函数
    sa.sa_handler = handler;
    if(is_restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(signo, &sa, NULL) != -1);
}

// 输出并向客户发送错误信息
void show_and_send_error(const int connfd, const std::string msg)
{
    std::cout << msg << std::endl;
    send(connfd, msg.c_str(), msg.size(), 0);
    close(connfd);
}

int main(int argc, char **argv)
{
    //argv[0] Oh-Server      argv[1] <port>    
    if(argc != 2)
    {
        std::cout << "usage: " << argv[0] << " <port>" << std::endl;
        return 0;
    }
    int port = atoi(argv[1]);
    
    //注册信号的信号处理函数
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池
    threadpool<http_process> *pool;
    try
    {
        pool = new threadpool<http_process>();
    }
    catch(std::runtime_error e)
    {
        std::cout << e.what() << std::endl;
        delete pool;
        return -1;
    }
    /* TCP服务器初始化过程
     * 1. socket()
     * 2. bind()
     * 3. listen()
     * 4. accept()
     * 5. process
     */
    //创建一个IPv4，字节流套接字，protocol=0：系统会根据套接字类型决定使用的传输层协议
    //创建成功返回一个非负的套接字描述符
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int optval = 1;

    //套接字设置为允许重用本地地址，成功返回0，出错返回-1
    int ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
            &optval, sizeof(optval));
    assert(ret >= 0);

    //用作bind、connect、recvfrom、sendto函数的参数，指明地址信息
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    //htonl函数将主机数转换成无符号 长整型 的网络字节序
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    //htons函数将主机数转换成无符号 短整型 的网络字节序
    servaddr.sin_port = htons(port);

    //绑定服务器的地址、端口
    ret = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    assert(ret >= 0);

    //listen(int sockfd, int backlog) backlog规定内核为相应套接字排队的最大连接数
    //函数成功返回0，出错返回-1
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    struct epoll_event evlist[MAX_EVENTS];
    //生成一个epoll专用的文件描述符。它其实是在内核申请一空间，
    //用来存放你想关注的socket fd上是否发生以及发生了什么事件。
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    //add_sockfd()函数内进行epoll事件注册，先注册要监听的事件类型
    add_sockfd(epollfd, listenfd, false);

    while(true)
    {
        int ready = epoll_wait(epollfd, evlist, MAX_EVENTS, -1);
        if((ready < 0) && (errno != EINTR))
        {
            std::cout << "epoll_wait failure" << std::endl;
            break;
        }

        for(int i = 0; i < ready; ++i)
        {
            int sockfd = evlist[i].data.fd;
            // 有新的客户连接到来
            if(sockfd == listenfd)
            {
                struct sockaddr_in clientaddr;
                socklen_t clientaddr_len = sizeof(clientaddr);
                int connfd = accept(listenfd, (struct sockaddr*)&clientaddr,
                        &clientaddr_len);
                if(connfd < 0)
                {
                    std::cout << "error: " << strerror(errno) << std::endl;
                    continue;
                }
                add_sockfd(epollfd, connfd, true);
            }
            // epoll出错
            else if(evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                close(sockfd);
            // 有新的请求到来
            else if(evlist[i].events & EPOLLIN)
            {
                pool->add(new http_process(epollfd, sockfd));
            }
        }
    }

    close(listenfd);
    close(epollfd);
    delete pool;
    return 0;
}
