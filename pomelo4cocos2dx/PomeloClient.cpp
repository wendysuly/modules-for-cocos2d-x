//
//  PomeloClient.cpp
//  test
//
//  Created by qiong on 14-2-17.
//
//

#include "PomeloClient.h"

PomeloClient PomeloClient::pc;
/*
 pomelo c callbacks
 */
void _pc_connect_cb(pc_connect_t* req, int status);
void _pc_event_cb(pc_client_t *client, const char *event, void *data);
void _pc_request_cb(pc_request_t *req, int status, json_t *resp);
void _pc_notify_cb(pc_notify_t *req, int status);

/* 暂时没用上
int _pc_handshake_cb(pc_client_t *client, json_t *msg);
pc_msg_t *_pc_msg_parse_cb(pc_client_t *client, const char *data,
                             size_t len);
void _pc_msg_parse_done_cb(pc_client_t *client, pc_msg_t *msg);
pc_buf_t _pc_msg_encode_cb(pc_client_t *client, uint32_t reqId,
                             const char* route, json_t *msg);
void _pc_msg_encode_done_cb(pc_client_t *client, pc_buf_t buf);
 */


PomeloClient& PomeloClient::getInstance()
{
    return pc;
}
PomeloClient::PomeloClient():_cbForConnect(nullptr),_client(nullptr)
{
    _scheduler = cocos2d::Director::getInstance()->getScheduler();
}

PomeloClient::~PomeloClient()
{
    stop();
}

int PomeloClient::connect(const char* addr, int port)
{
    assert(!_client);   //_client must be nullptr
    _client = pc_client_new();
    
    struct sockaddr_in address;
    
    memset(&address, 0, sizeof(struct sockaddr_in));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(addr);
    
    // try to connect to server.
    int ret = pc_client_connect(_client, &address);
    if(ret) {
        cocos2d::log("fail to connect server %s on port %d",addr,port);
        stop();
    }
    return ret;
}

int PomeloClient::connectAsync(const char* addr, int port, cb1I callback)
{
    assert(!_cbForConnect); //_cbForConnect must be nullptr
    _cbForConnect = callback;
    assert(!_client);       //_client must be nullptr
    _client = pc_client_new();
    
    struct sockaddr_in address;
    
    memset(&address, 0, sizeof(struct sockaddr_in));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(addr);
    
    // try to connect to server.
    pc_connect_t* connect_req = pc_connect_req_new(&address);
    int ret = pc_client_connect2(_client, connect_req, _pc_connect_cb);
    if(ret){
        cocos2d::log("fail to async connect to server %s on port %d",addr,port);
        stop();
    }
    return ret;
}

void PomeloClient::stop()
{
    if(_client){
        pc_client_destroy(_client);
        _client = nullptr;
        _cbForConnect = nullptr;
    }
    _allRequests.clear();
    _allEvents.clear();
    _allNotifies.clear();
}
void PomeloClient::regDisconnectCallback(cb1Json callback)
{
    addEventListener(PC_EVENT_DISCONNECT, callback);
}
void PomeloClient::delDisconnectCallback()
{
    removeEventListener(PC_EVENT_DISCONNECT);
}
void PomeloClient::regTimeoutCallback(cb1Json callback)
{
    addEventListener(PC_EVENT_TIMEOUT,callback);
}
void PomeloClient::delTimeoutCallback()
{
    removeEventListener(PC_EVENT_TIMEOUT);
}
void PomeloClient::regOnKickCallback(cb1Json callback)
{
    addEventListener(PC_EVENT_KICK,callback);
}
void PomeloClient::delOnKickCallback()
{
    removeEventListener(PC_EVENT_KICK);
}

int PomeloClient::addEventListener(const char* eventName, cb1Json callback)
{
    assert(_client);
    _allEvents[eventName] = callback;
    return pc_add_listener(_client, eventName, _pc_event_cb);
}

void PomeloClient::removeEventListener(const char* eventName)
{
    assert(_client);
    _allEvents.erase(_allEvents.find(eventName));
    pc_remove_listener(_client, eventName, _pc_event_cb);//删除一个监听也有回调？
}

int PomeloClient::request(const char* route, json_t* msg, cb1I1Json callback)
{
    assert(_client);
    pc_request_t* req = pc_request_new();
    _allRequests[route] = callback;
    return pc_request(_client, req, route, msg, _pc_request_cb);
}

int PomeloClient::notify(const char* route, json_t* msg, cb1I callback)
{
    assert(_client);
    pc_notify_t *notify = pc_notify_new();
    _allNotifies[route] = callback;
    return pc_notify(_client, notify, route, msg, _pc_notify_cb);
}

void _pc_connect_cb(pc_connect_t* req, int status)
{
    pc_connect_req_destroy(req);
    /*add the function object to the safe UI thread*/
    PomeloClient& pc = PomeloClient::getInstance();
    pc.getScheduler()->performFunctionInCocosThread(bind(pc.getCbForConnect(), status));
}

void _pc_event_cb(pc_client_t *client, const char *event, void *data)
{
    json_t* json = (json_t* )data;
    const char* msg = json_dumps(json, 0);
    if(msg)
        cocos2d::log("on event: %s, %s", event, msg);
    PomeloClient& pc = PomeloClient::getInstance();
    if (strcmp(event, PC_EVENT_DISCONNECT) == 0 ||
        strcmp(event, PC_EVENT_TIMEOUT) == 0||
        strcmp(event, PC_EVENT_KICK) == 0) {
        pc.stop();
    }
    /*add the function object to the safe UI thread*/
    pc.getScheduler()->performFunctionInCocosThread(bind(pc.getCbForEvent(event), json));
}

void _pc_request_cb(pc_request_t *req, int status, json_t *resp)
{
    const char* resp_str = json_dumps(resp, 0);
    if(resp_str)
    {   cocos2d::log("on request: %s, %s", req->route, resp_str);
    }
    /*add the function object to the safe UI thread*/
    PomeloClient& pc = PomeloClient::getInstance();
    pc.getScheduler()->performFunctionInCocosThread(bind(pc.getCbForRequest(req->route), status, resp));
    
    // release relative resource with pc_request_t
    json_t *msg = req->msg;
    json_decref(msg);
    pc_request_destroy(req);
}

void _pc_notify_cb(pc_notify_t *req, int status)
{
    /*add the function object to the safe UI thread*/
    PomeloClient& pc = PomeloClient::getInstance();
    pc.getScheduler()->performFunctionInCocosThread(bind(pc.getCbForNotify(req->route), status));
    // release resources
    json_t *msg = req->msg;
    json_decref(msg);
    pc_notify_destroy(req);
}