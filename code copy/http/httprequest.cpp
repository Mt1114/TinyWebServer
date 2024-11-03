#include "httprequest.h"

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML = {
    "/index",
    "/register",
    "/login",
    "/welcome",
    "/video",
    "/picture",
};

const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG = {
    {"/register.html", 0},
    {"/login.html", 1},
};

void HttpRequest::Init()
{
    method_ = path_ = version_ = body_ = "";
    header_.clear();
    post_.clear();
    state_ = REQUEST_LINE;
}

bool HttpRequest::IsKeepAlive() const
{
    if (header_.count("Connection") == 1)
    {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

// 16进制转化为10进制
int HttpRequest::ConverHex(char ch)
{
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return ch;
}

bool HttpRequest::parse(Buffer &buff)
{
    const char CRLF[] = "\r\n";
    if (buff.ReadableBytes() == 0)
    { // 没有可读的字节
        return false;
    }
    while (buff.ReadableBytes() && state_ != FINISH)
    {
        const char *lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        std::string line(buff.Peek(), lineEnd);
        switch (state_)
        {
        case REQUEST_LINE:
            if (!ParseRequestLine_(line))
            {
                return false;
            }
            ParsePath_();
            break;
        case HEADERS:
            ParseHeader_(line);
            if (buff.ReadableBytes() <= 2)
            {
                state_ = FINISH;
            }
            break;
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }
        if (lineEnd == buff.BeginWrite())
        {
            buff.RetrieveAll();
            break;
        }
        buff.RetrieveUntil(lineEnd + 2); // 跳过回车换行
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

// 解析路径
void HttpRequest::ParsePath_()
{
    if (path_ == "/")
    {
        path_ = "/index.html";
    }
    else
    {
        for (auto &item : DEFAULT_HTML)
        {
            if (item == path_)
            {
                path_ += ".html";
                break;
            }
        }
    }
}

bool HttpRequest::ParseRequestLine_(const std::string &line)
{
    regex pattern("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, pattern))
    {
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;
        return true;
    }
    else
    {
        LOG_ERROR("RequestLine Error");
        return false;
    }
}

void HttpRequest::ParseHeader_(const std::string &line)
{
    regex pattern("^([^:]*): ?(.*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, pattern))
    {
        header_[subMatch[1]] = subMatch[2];
    }
    else
    {
        state_ = BODY;
    }
}

void HttpRequest::ParseFromUrlencoded_()
{
    if (body_.size() == 0)
    {
        return;
    }
    int n = body_.size();
    int num = 0;
    int end = 0, start = 0;
    std::string key = "", value = "";
    for (; end < n; end++)
    {
        char ch = body_[end];
        switch (ch)
        {
        case '=':
            key = body_.substr(start, end - start);
            start = end + 1;
            break;
        case '&':
            value = body_.substr(start, end - start);
            post_[key] = value;
            start = end + 1;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        case '+':
            body_[end] = ' ';
            break;
        case '%':
            num = ConverHex(body_[end + 1]) * 16 + ConverHex(body_[end + 2]);
            body_[end + 2] = num % 10 + '0';
            body_[end + 1] = num / 10 + '0';
            end += 2;
            break;
        default:
            break;
        }
    }
    assert(start <= end);
    if (start < end && post_.count(key) == 0)
    {
        value = body_.substr(start, end - start);
        post_[key] = value;
    }
}

void HttpRequest::ParsePost_()
{
    if (method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded")
    {
        ParseFromUrlencoded_();
        if (DEFAULT_HTML_TAG.count(path_))
        {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if (tag == 1 || tag == 0)
            {
                bool isLogin = (tag == 1);
                if (isLogin)
                {
                    if (UserVerify("username", "password", isLogin))
                    {
                        path_ = "/welcome.html";
                    }
                    else
                    {
                        path_ = "/error.html";
                    }
                }
            }
        }
    }
}

void HttpRequest::ParseBody_(const std::string &line)
{
    body_ = line;
    ParsePost_();
    state_ = FINISH; // 状态转换为下一个状态
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

bool HttpRequest::UserVerify(const std::string &name,
                             const std::string &pwd, bool isLogin)
{
    if (name == "" || pwd == "")
    {
        return false;
    }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL *sql;
    SqlConnRAII(&sql, SqlConnPool::Instance());
    assert(sql);

    int flag = false;
    unsigned int num = 0;
    MYSQL_RES *res = nullptr;
    MYSQL_FIELD *field = nullptr;
    char order[256] = {0};
    if (!isLogin)
    {
        flag = true;
    }
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);
    if (mysql_query(sql, order))
    {
        mysql_free_result(res);
        return false;
    }

    res = mysql_store_result(sql);
    num = mysql_num_fields(res);
    field = mysql_fetch_fields(res);

    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password = row[1];
        if (isLogin)
        {
            if (password == pwd)
            {
                flag = true;
            }
            else
            {
                flag = false;
                LOG_INFO("pwd error!");
            }
        }
        else
        {
            flag = false;
            LOG_INFO("user used!");
        }
    }
    mysql_free_result(res);

    if (!isLogin && flag == true)
    {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256, "INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG("%s", order);
        if (mysql_query(sql, order))
        {
            LOG_DEBUG("Insert error!");
            flag = false;
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG("UserVerify success!!");
    return flag;
}

std::string HttpRequest::path() const
{
    return path_;
}

std::string &HttpRequest::path()
{
    return path_;
}
std::string HttpRequest::method() const
{
    return method_;
}

std::string HttpRequest::version() const
{
    return version_;
}

std::string HttpRequest::GetPost(const std::string &key) const
{
    assert(key != "");
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char *key) const
{
    assert(key != nullptr);
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}