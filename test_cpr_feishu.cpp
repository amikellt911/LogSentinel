#include <cpr/cpr.h>
#include <iostream>
#include <memory>
#include <string>

cpr::Session& getTlsSession()
{
    static thread_local std::unique_ptr<cpr::Session> session = []()
    {
        auto s = std::make_unique<cpr::Session>();
        s->SetTimeout(std::chrono::seconds(5));
        s->SetHeader(cpr::Header{{"Content-Type", "application/json"}});
        return s;
    }();
    return *session;
}

int main() {
    cpr::Session& s = getTlsSession();
    s.SetUrl(cpr::Url{"https://open.feishu.cn/open-apis/bot/v2/hook/0f38f06f-0d50-4fec-ade1-881242ef6f7a "});
    s.SetBody(cpr::Body{"{}"});
    cpr::Response r = s.Post();
    std::cout << "Status: " << r.status_code << std::endl;
    std::cout << "Msg: " << r.text << std::endl;
    
    return 0;
}
