/*

API MODULE

Copyright (C) 2016-2019 by Xose Pérez <xose dot perez at gmail dot com>
Copyright (C) 2020-2021 by Maxim Prokhorov <prokhorov dot max at outlook dot com>

*/

#pragma once

#include "espurna.h"

#include <functional>

#include "api_path.h"

#if WEB_SUPPORT
#include "api_impl.h"
#include "web.h"

bool apiAuthenticateHeader(AsyncWebServerRequest*, const String& key);
bool apiAuthenticateParam(AsyncWebServerRequest*, const String& key);
bool apiAuthenticate(AsyncWebServerRequest*);

namespace espurna {
namespace api {

using BasicHandler = std::function<bool(Request&)>;
using JsonHandler = std::function<bool(Request&, JsonObject& reponse)>;

} // namespace api
} // namespace espurna

void apiRegister(String path,
    espurna::api::BasicHandler&& get,
    espurna::api::BasicHandler&& put);

void apiRegister(String path,
    espurna::api::JsonHandler&& get,
    espurna::api::JsonHandler&& put);

bool apiError(espurna::api::Request&);
bool apiOk(espurna::api::Request&);
#endif

using ApiRequest = espurna::api::Request;
using ApiBasicHandler = espurna::api::BasicHandler;
using ApiJsonHandler = espurna::api::JsonHandler;

void apiCommonSetup();
bool apiEnabled();
bool apiRestFul();
String apiKey();

void apiSetup();
