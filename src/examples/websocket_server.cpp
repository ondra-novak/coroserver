#include <coroserver/io_context.h>

#include <iostream>
#include <sstream>
#include <coroserver/http_server_request.h>
#include <coroserver/http_server.h>
#include <coroserver/http_ws_server.h>
#include <cocls/publisher.h>

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


using MyPublisher = cocls::publisher<std::string>;
using MySubscriber = cocls::subscriber<std::string>;

std::string to_id(const void *ptr) {
    std::ostringstream s;
    s << ptr;
    return s.str();
}

cocls::async<void> reader(ws::Stream stream, MyPublisher &publisher, const MySubscriber *instance) {
    do {
        ws::Message &msg = co_await stream.read();
        switch (msg.type) {
            case ws::Type::text: {
                std::string txt(msg.payload);
                publisher.publish(std::move(txt));
            }break;
            case ws::Type::connClose:
                publisher.kick(instance);
                co_return;
            default:
                break;

        }
    }
    while (true);
}

cocls::async<void> writer(ws::Stream s, MyPublisher &publisher) {
    MySubscriber sbs(publisher);
    reader(s, publisher, &sbs).detach();
    while (co_await sbs.next()) {
       std::string msg (std::move(sbs.value()));
       if (!co_await s.write({msg, ws::Type::text})) {
           break;
       }
    }

}

cocls::future<void> ws_handler(http::ServerRequest &req, MyPublisher &publisher) {
    ws::Server server({});
    auto fut = server(req);
    if (co_await fut.has_value()) {
        writer(*fut, publisher).detach();
    } else {
        req.set_status(400);
    }
}



int main(int, char **) {

    MyPublisher publisher;

    auto addrs = PeerName::lookup(":10000","");
    ContextIO ctx = ContextIO::create(4);
    auto listener = ctx.accept(std::move(addrs));
    http::Server server;

    server.set_handler("/ws", http::Method::GET, [&](http::ServerRequest &req){
        return ws_handler(req, publisher);
    });

    server.set_handler("/", http::Method::GET, [&](http::ServerRequest &req, std::string_view vpath){
            if (vpath.empty()) {
                req.content_type(http::ContentType::text_html_utf8);
                return req.send(page);
            } else {
                return cocls::future<bool>::set_value(false);
            }
    });
    auto task = server.start(std::move(listener),http::DefaultLogger([](std::string_view line){
        std::cout << line << std::endl;
    }));
    std::cout << "Press enter to stop server:" << std::endl;
    std::cin.get();
    ctx.stop();
    task.join();

#ifdef COCLS_DEFINE_SET_CORO_NAME
    mon_exit.store(true);
    cocls::coro_monitor_event();
    mon_thread.join();
#endif

    return 0;
}
