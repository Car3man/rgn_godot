#include "Core/RGNCore.h"
#include "Core/RGNAuth.h"
#include "Analytics/Analytics.h"
#include "DeepLink/DeepLink.h"
#include "WebForm/WebForm.h"
#include "Environment/Environment.h"
#include "SharedPrefs/SharedPrefs.h"
#include "Http/Http.h"
#include "Crypto/hmac.h"
#include "Crypto/sha256.h"
#include "Utility/Logger.h"
#include "json.hpp"

namespace RGN {
    std::string RGNCore::_appId;
    std::string RGNCore::_apiKey;
    RGNEnvironmentTarget RGNCore::_environmentTarget;
    bool RGNCore::_useFunctionsEmulator;
    std::string RGNCore::_emulatorHostAndPort;

    void RGNCore::Initialize(RGNConfigureData configureData, std::function<void()> onInitialize) {
        _appId = configureData.appId;
        _apiKey = configureData.apiKey;
        _environmentTarget = configureData.environmentTarget;
        _useFunctionsEmulator = configureData.useFunctionsEmulator;
        _emulatorHostAndPort = configureData.emulatorHost + ":" + configureData.emulatorPort;

        RGNAuth::Initialize(configureData.autoGuestLogin);
        DeepLink::Initialize();
        DeepLink::Start();
        WebForm::Initialize();
        Analytics::Initialize();

        if (configureData.autoGuestLogin && !RGNAuth::IsLoggedIn()) {
            RGN::RGNAuth::SignInAnonymously([onInitialize](bool isLoggedIn) {
                if (onInitialize) {
                    onInitialize();
                }
            });
        }
        else {
            if (onInitialize) {
                onInitialize();
            }
        }

        LogSdkInitializeEvent();
    }

    void RGNCore::LogSdkInitializeEvent() {
        std::string sdkInitializeEventFlag;
        if (SharedPrefs::Load("sdk_initialize_event_flag", sdkInitializeEventFlag) && !sdkInitializeEventFlag.empty()) {
            return;
        }
        nlohmann::json info;
        info["ProcessorType"] = Environment::GetProcessorType();
        info["ProcessorCount"] = Environment::GetProcessorCount();
        info["SystemMemorySize"] = Environment::GetSystemMemorySize();
        info["GraphicsDeviceName"] = Environment::GetGraphicsDeviceName();
        info["GraphicsMemorySize"] = Environment::GetGraphicsMemorySize();
        info["OperatingSystem"] = Environment::GetOperatingSystem();
        info["OperatingSystemFamily"] = Environment::GetOperatingSystemFamily();
        info[Environment::GetEngineName() + "Version"] = Environment::GetEngineVersion();
        info["RGNSDKVersion"] = GetSdkVersion();
        info["AppIdentifier"] = Environment::GetAppIdentifier();
        info["AppInstallerName"] = Environment::GetAppInstallerName();
        Analytics::LogEvent("rgn_core_build_once_per_install", info.dump());
        SharedPrefs::Save("sdk_initialize_event_flag", "true");
    }

    void RGNCore::SetEmulator(bool useEmulator, std::string endpoint) {
        _useFunctionsEmulator = useEmulator;
        _emulatorHostAndPort = endpoint;
    }

    void RGNCore::Update() {
        Http::Update();
        WebForm::Update();
    }

    void RGNCore::InternalCallAPI(const std::string& name, const std::string& body,
        const std::function<void(const std::string&)>& complete,
        const std::function<void(const int, const std::string&)>& fail,
        bool computeHmac, CancellationToken cancellationToken) {
        HttpHeaders headers;
        headers.add("Content-type", "application/json");
        if (!_appId.empty()) {
            headers.add("app-id", _appId);
        }
        if (!RGNAuth::GetIdToken().empty()) {
            headers.add("Authorization", "Bearer " + RGNAuth::GetIdToken());
        }
        if (computeHmac) {
            headers.add("hmac", hmac<SHA256>(body, _apiKey));
        }
        std::string url = GetApiUrl() + name;
        Http::Request(url, HttpMethod::POST, headers, body, [name, body, complete, fail, computeHmac, cancellationToken](HttpResponse httpResponse) {
            if (cancellationToken.isCancellationRequested()) {
                if (fail) {
                    fail(400, "The request was cancelled");
                }
                return;
            }
            int httpResponseCode = httpResponse.getResponseCode();
            std::string httpResponseBody = httpResponse.getResponseBody();
            if (httpResponseCode == 200) {
                if (complete) {
                    complete(httpResponseBody);
                }
            } else if (httpResponseCode == 401) {
                if (!RGNAuth::GetRefreshToken().empty()) {
                    RGNAuth::RefreshTokens([name, body, complete, fail, computeHmac, cancellationToken, httpResponseCode, httpResponseBody](bool successRefreshTokens) {
                        if (cancellationToken.isCancellationRequested()) {
                            if (fail) {
                                fail(400, "The request was cancelled");
                            }
                            return;
                        }
                        if (successRefreshTokens) {
                            InternalCallAPI(name, body, complete, fail, computeHmac, cancellationToken);
                        } else {
                            RGNAuth::SignOut();
                            if (fail) {
                                fail(httpResponseCode, httpResponseBody);
                            }
                        }
                        });
                } else {
                    RGNAuth::SignOut();
                    if (fail) {
                        fail(httpResponseCode, httpResponseBody);
                    }
                }
            } else {
                if (fail) {
                    fail(httpResponseCode, httpResponseBody);
                }
            }
            });
    }

    void RGNCore::NonAuthInternalCallAPI(const std::string& name, const std::string& body,
        const std::function<void(const std::string&)>& complete, const std::function<void(const int, const std::string&)>& fail) {
        HttpHeaders headers;
        headers.add("Content-type", "application/json");
        if (!_appId.empty()) {
            headers.add("app-id", _appId);
        }
        std::string url = GetApiUrl() + name;
        Http::Request(url, HttpMethod::POST, headers, body, [complete, fail](HttpResponse httpResponse) {
            int httpResponseCode = httpResponse.getResponseCode();
            std::string httpResponseBody = httpResponse.getResponseBody();
            if (httpResponseCode == 200) {
                if (complete) {
                    complete(httpResponseBody);
                }
            }
            else {
                if (fail) {
                    fail(httpResponseCode, httpResponseBody);
                }
            }
            });
    }

    std::string RGNCore::GetApiRegion() {
        return "us-central1";
    }

    std::string RGNCore::GetApiProjectId() {
        switch (_environmentTarget) {
        case RGNEnvironmentTarget::DEVELOPMENT:
            return "readymaster-development";
        case RGNEnvironmentTarget::STAGING:
            return "readysandbox";
        case RGNEnvironmentTarget::PRODUCTION:
            return "readymaster-2b268";
        }
        return "";
    }

    std::string RGNCore::GetApiUrl() {
        if (_useFunctionsEmulator) {
            return "http://" + _emulatorHostAndPort + "/" + GetApiProjectId() + "/" + GetApiRegion() + "/";
        }
        return "https://" + GetApiRegion() + "-" + GetApiProjectId() + ".cloudfunctions.net/";
    }

    std::string RGNCore::GetOAuthUrl() {
        switch (_environmentTarget) {
        case RGNEnvironmentTarget::DEVELOPMENT:
            return "https://development-oauth.ready.gg/?url_redirect=";
        case RGNEnvironmentTarget::STAGING:
            return "https://staging-oauth.ready.gg/?url_redirect=";
        case RGNEnvironmentTarget::PRODUCTION:
            return "https://oauth.ready.gg/?url_redirect=";
        }
        return "";
    }

    std::string RGNCore::GetStorageBucket() {
        switch (_environmentTarget) {
        case RGNEnvironmentTarget::DEVELOPMENT:
            return "readymaster-development";
        case RGNEnvironmentTarget::STAGING:
            return "readysandbox";
        case RGNEnvironmentTarget::PRODUCTION:
            return "readymaster-2b268";
        }
        return "";
    }

    std::string RGNCore::GetAppId() {
        return _appId;
    }

    std::string RGNCore::GetApiKey() {
        return _apiKey;
    }

    RGNEnvironmentTarget RGNCore::GetEnvironmentTarget() {
        return _environmentTarget;
    }

    std::string RGNCore::GetUserId() {
        return RGNAuth::GetUserId();
    }

    std::string RGNCore::GetSdkVersion() {
        return "0.10.0";
    }
}