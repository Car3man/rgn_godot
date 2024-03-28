#pragma once

#include "Http/HttpMethod.h"
#include "Http/HttpHeaders.h"
#include "Http/HttpResponse.h"
#include <string>
#include <functional>
#include <godot_cpp/classes/http_client.hpp>

namespace RGN {
    class HttpRequestGodot {
    private:
        std::string url;
        HttpMethod method;
        HttpHeaders headers;
        std::string body;
        std::function<void(HttpResponse)> callback;
        godot::Ref<godot::TLSOptions> httpTLS;
        godot::HTTPClient* httpClient;
        static godot::HTTPClient::Method GetGodotRequestMethod(HttpMethod method);
    public:
        HttpRequestGodot();
        HttpRequestGodot(const HttpRequestGodot& request);
        HttpRequestGodot& operator=(const HttpRequestGodot& other);
        HttpRequestGodot(HttpRequestGodot&& other) noexcept;
        HttpRequestGodot& operator=(HttpRequestGodot&& other) noexcept;
        ~HttpRequestGodot();
        void setUrl(std::string url);
        void setMethod(HttpMethod method);
        void setHeaders(HttpHeaders headers);
        void addHeader(std::string key, std::string value);
        void setBody(std::string body);
        void setCallback(const std::function<void(HttpResponse)>& callback);
        bool processRequest();
        bool update();
    };
}