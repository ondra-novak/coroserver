#include <coroserver/io_context.h>

#include <iostream>
#include <sstream>
#include <coroserver/http_server_request.h>
#include <coroserver/http_server.h>
#include <coroserver/http_ws_server.h>
#include <coro.h>

#include <variant>
#include <vector>
#include <coroserver/peername.h>
using namespace coroserver;

std::string_view page = R"html(<!DOCTYPE html>
<html>
<head>
<title>WebSocket test</title>
</head>
<body>
<div id="area">
</div>
<div>
Name <input type="text" id="name" size="10"> Type text (Enter): <input type="text" id="text" size="50">
</div>
<script type="text/javascript">
var loc = window.location, new_uri;
if (loc.protocol === "https:") {
    new_uri = "wss:";
} else {
    new_uri = "ws:";
}
new_uri += "//" + loc.host;
new_uri += loc.pathname + "./ws";
let ws = new WebSocket(new_uri);
ws.onmessage = m => {
    var el = document.createElement("P");
    el.innerText = m.data;
    document.getElementById("area").appendChild(el);
}

document.getElementById("text").addEventListener("keypress",ev=>{
   if (ev.key == "Enter") {
        var n = document.getElementById("name").value;
        ws.send(n+": "+ev.target.value);
        ev.target.value = "";
    }
});

function forge_nick() {
    var n = "";
    var s2 = "aeiouyaeiouyaeiouy1234567890";
    var s1 = "qwrtpsdfghjklzxcvbnm";
    var i;
    for (i = 0; i < 7; i++) {
        var x = Math.random();
        if (i & 1) {
            var idx = Math.floor(x*s1.length);
            n = n + s1[idx];
        } else {
            var idx = Math.floor(x*s2.length);
            n = n + s2[idx];
        }
    }
    document.getElementById("name").value = n;
}

forge_nick();

</script>

</body>
</html>
)html";

using MyPublisher = coro::distributor<std::string, std::mutex>;
using SubscriberID = MyPublisher::ID;

/// reader - detached coroutine, which reads data and pushes them to distributor
/**
 * @param stream web socket stream
 * @param publisher reference to distributor acting as publisher
 * @param id identifier of the subscribtion
 * @return asynchronous function (coroutine)
 */
coro::async<void> reader(ws::Stream stream, MyPublisher &publisher, SubscriberID id) {
    //this is infinite cycle
    while (true) {
        //receive websocket message
        const ws::Message &msg = co_await stream.receive();
        //depend on type
        switch (msg.type) {
            //if this text
            case ws::Type::text: {
                //create string
                std::string txt(msg.payload);
                //publish string
                publisher.publish(std::move(txt));
            }break;
            //connection closed
            case ws::Type::connClose:
                //unsubscribe
                publisher.drop(id);
                //exit
                co_return;
            default:
                //ignore any other message
                break;

        }
    }

}

///writer - monitors published messages and sends them to websocket connectionb
/**
 * Automatically starts reader in detached mode
 *
 * @param s websocket connection
 * @param publisher reference to shared publisher/distributor
 * @return async function
 */
coro::async<void> writer(ws::Stream s, MyPublisher &publisher) {
    //create queue for published data (because
    MyPublisher::queue<> q;
    q.subscribe(publisher);
    reader(s, publisher, &q).detach();
    bool cont;
    do {
        auto msg = q.pop();
        cont = co_await msg.has_value();
        if (cont) {
            std::string str = msg;
            bool st = co_await s.send(ws::Message{str, ws::Type::text});
            if (!st) {
                cont = false;
            }
        }
    } while (cont);
}

coro::future<void> ws_handler(http::ServerRequest &req, MyPublisher &publisher) {
    try {
        ws::Stream s = co_await ws::Server::accept(req);
        req.log_message("Opened websocket connection");
        co_await writer(std::move(s), publisher);
        req.log_message("Closed websocket connection");
    } catch (const coro::broken_promise_exception &) {
        req.set_status(400);
    }
}



int main(int, char **) {

    MyPublisher publisher;

    auto addrs = PeerName::lookup(":10000","");
    ContextIO ctx = ContextIO::create(1);
    auto listener = ctx.accept(std::move(addrs));
    http::Server server;

    server.set_handler("/ws", http::Method::GET, [&](http::ServerRequest &req){
        return ws_handler(req, publisher);
    });

    server.set_handler("/", http::Method::GET, [&](http::ServerRequest &req, std::string_view vpath) -> coro::future<bool> {
            if (vpath.empty()) {
                req.content_type(http::ContentType::text_html_utf8);
                return req.send(std::string(page));
            } else {
                return false;
            }
    });
    auto task = server.start(std::move(listener),http::DefaultLogger([](std::string_view line){
        std::cout << line << std::endl;
    }));
    std::cout << "Press enter to stop server:" << std::endl;
    std::cin.get();
    ctx.stop();
    task.wait();

    return 0;
}
