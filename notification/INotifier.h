#pragma once
#include<string>
#include<nlohmann/json.hpp>

class INotifier {
public:
    virtual ~INotifier() = default;
    virtual void notify(const std::string& trace_id,const nlohmann::json& content) = 0;
};