#include "httpconn.h"

HttpConn::HttpConn()
{
    fd_ = -1;
    addr_ = {0};
    isClose_ = true;
}

HttpConn::~HttpConn()
{
    Close();
}

const char *HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;
bool HttpConn::isET;

void HttpConn::init(int sockFd, const sockaddr_in &addr)
{
    assert(sockFd > 0);
    userCount++;
    fd_ = sockFd;
    addr_ = addr;
    readBuff_.RetrieveAll();
    writeBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

ssize_t HttpConn::read(int *saveErrno)
{
    ssize_t len;
    do
    {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len < 0)
        {
            break;
        }
    } while (isET);
    return len;
}

ssize_t HttpConn::write(int *saveErrno)
{
    ssize_t len;
    do
    {
        len = writeBuff_.WriteFd(fd_, saveErrno);
        if (len <= 0)
        {
            *saveErrno = errno;
            break;
        }
        if (iov_[0].iov_len + iov_[1].iov_len == 0)
        {
            break;
        }
        else if (static_cast<size_t>(len) > iov_[0].iov_len)
        {
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            iov_[1].iov_base = (uint8_t *)iov_[1].iov_base + (len - iov_[0].iov_len);
            if (iov_[0].iov_len)
            {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else
        {
            iov_[0].iov_len -= len;
            iov_[0].iov_base = (uint8_t *)iov_[0].iov_base + len;
            writeBuff_.Retrieve(len);
        }
    } while (isET || ToWriteBytes() > 10240);
    return len;
}
// 可读返回true
bool HttpConn::process()
{
    request_.Init();
    if (readBuff_.ReadableBytes() <= 0)
    {
        return false;
    }
    else if (request_.parse(readBuff_))
    { // 解析成功
        LOG_DEBUG("%s", request_.path().c_str());
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    }
    else
    {
        response_.Init(srcDir, request_.path(), false, 400);
    }
    response_.MakeResponse(writeBuff_);
    iov_[0].iov_base = const_cast<char *>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;
    if (response_.File() && response_.FileLen() > 0)
    {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen(), iovCnt_, ToWriteBytes());
    return true;
}

int HttpConn::GetFd() const
{
    return fd_;
};

struct sockaddr_in HttpConn::GetAddr() const
{
    return addr_;
}

const char *HttpConn::GetIP() const
{
    return inet_ntoa(addr_.sin_addr);
}

int HttpConn::GetPort() const
{
    return addr_.sin_port;
}

void HttpConn::Close()
{
    response_.UnmapFile();
    if (isClose_ == false)
    {
        isClose_ = true;
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}