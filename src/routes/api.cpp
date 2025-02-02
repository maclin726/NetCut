#include "routes/api.hpp"
#include "models/controller.hpp"
#include "models/host.hpp"
#include "utils/json.hpp"
#include "utils/socket_utils.hpp"
#include "utils/thread_utils.hpp"

#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/http_headers.h>

#include <thread>
#include <memory>
#include <algorithm>
#include <vector>
#include <map>

#include <iostream>

using namespace Pistache;
using json = nlohmann::json;

void start_server(uint16_t listening_port, const Controller &controller) {
    Port port(listening_port);

    unsigned int num_threads = std::thread::hardware_concurrency();
    unsigned int server_num_threads = std::max(1U, num_threads / 4);

    Address addr(Ipv4::any(), port);

    ApiEndpoint api(addr, controller);

    api.init(server_num_threads);
    api.start();
}

void ApiEndpoint::setup_routes() {
    using namespace Rest;
    Routes::Get(router, "/ping", Routes::bind(&ApiEndpoint::ping, this));
    Routes::Get(router, "/get_targets", Routes::bind(&ApiEndpoint::get_targets, this));
    Routes::Get(router, "/get_status/:ip", Routes::bind(&ApiEndpoint::get_status, this));
    Routes::Post(router, "/action/:ip", Routes::bind(&ApiEndpoint::action, this));
    Routes::Post(router, "/quit", Routes::bind(&ApiEndpoint::quit, this));

    Routes::Get(router, "/get_info", Routes::bind(&ApiEndpoint::get_info, this));
    Routes::Post(router, "/set_info", Routes::bind(&ApiEndpoint::set_info, this));
}

ApiEndpoint::ApiEndpoint(Address addr, const Controller &controller)
            : httpEndpoint(std::make_shared<Http::Endpoint>(addr)), controller(controller) {}

void ApiEndpoint::init(size_t num_threads) {
    auto opts = Http::Endpoint::options().threads(static_cast<int>(num_threads));
    httpEndpoint->init(opts);
    setup_routes();
}

void ApiEndpoint::start() {
    httpEndpoint->setHandler(router.handler());
    httpEndpoint->serve();
}

// routes
void ApiEndpoint::ping(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.send(Http::Code::Ok, "OK");
}

void ApiEndpoint::quit(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");

    controller.recover_all_hosts(); // being nice by recovering the network for targets before exiting
    Socket::close_sockets();
    Thread::stop_all_threads();
    auto res = response.send(Http::Code::Ok, "OK");
    res.then(
        [](ssize_t bytes) { exit(EXIT_SUCCESS); },
        Async::NoExcept
    );
}

void ApiEndpoint::get_targets(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Http::Header::ContentType>(MIME(Application, Json));

    controller.scan_targets();
    std::vector<Host> hosts = controller.get_targets();

    std::vector<std::map<std::string, std::string>> v;

    for (const Host &host : hosts) {
        v.push_back({
            { "IP Address", host.get_ip() },
            { "MAC Address", host.get_mac() },
            { "Status", host.is_cut() ? "Cut" : "Normal" },
        });
    }

    json j_vec(v);
    response.send(Http::Code::Ok, j_vec.dump());
}

void ApiEndpoint::action(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Http::Header::ContentType>(MIME(Application, Json));

    std::string target_ip = request.param(":ip").as<std::string>();

    ACTION_STATUS status = controller.action(target_ip);
    std::map<std::string, std::string> res;
    auto http_return_code = Http::Code::Ok; // default

    res["Target"] = target_ip;
    
    switch (status) {
      case ACTION_STATUS::TARGET_NOT_FOUND:
        res["Status"] = "Target Not Found";
        http_return_code = Http::Code::Not_Found;
        break;
      case ACTION_STATUS::CUT_SUCCESS:
        res["Status"] = "Cut";
        break;
      case ACTION_STATUS::RECOVER_SUCCESS:
        res["Status"] = "Recovered";
        break;
    }

    json j_map(res);
    response.send(http_return_code, j_map.dump());
}

void ApiEndpoint::get_status(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Http::Header::ContentType>(MIME(Application, Json));

    controller.scan_targets();

    std::string target_ip = request.param(":ip").as<std::string>();
    Host host = controller.get_host(target_ip);
    std::map<std::string, std::string> res;
    auto http_return_code = Http::Code::Ok; // default

    res["Target"] = target_ip;

    switch (host.get_status()) {
      case Status::NOT_EXIST:
        res["Status"] = "Target Not Found";
        http_return_code = Http::Code::Not_Found;
        break;
      case Status::CUT:
        res["Status"] = "Cut";
        break;
      case Status::NORMAL:
        res["Status"] = "Normal";
        break;
    }

    json j_map(res);
    response.send(http_return_code, j_map.dump());
}

void ApiEndpoint::get_info(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Http::Header::ContentType>(MIME(Application, Json));


    std::map<std::string, std::string> res;
    auto http_return_code = Http::Code::Ok; // default
    res["atk"] = std::to_string(controller.get_atk_value());
    res["def"] = std::to_string(controller.get_def_value());

    json j_map(res);
    response.send(http_return_code, j_map.dump());
}

void ApiEndpoint::set_info(const Rest::Request& request, Http::ResponseWriter response) {
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Http::Header::ContentType>(MIME(Application, Json));

    std::map<std::string, std::string> res;
    auto http_return_code = Http::Code::Ok; // default
    
    json j_map = json::parse(request.body());
    controller.set_atk_value(std::stoi(std::string(j_map["atk"])));
    controller.set_def_value(std::stoi(std::string(j_map["def"])));

    // response.send(http_return_code, request.body());
    response.send(http_return_code, j_map.dump());
}
