#include "http_server.h"

namespace coroserver {

namespace http {

std::string_view Server::error_handler_prefix ( "error_");
const Server::RequestFactory Server::secure = [](Stream s){
    return ServerRequest(std::move(s),true);
};

IHandler::Ret Server::send_error_page(ServerRequest &req) {
    //lock the lock
    std::shared_lock lk(_mx);
    //retrieve status
    int status = req.get_status();
    //zero status is set to 404
    if (status == 0) {
        status = 404;
        //set the status to request to set apropriate message
        req.set_status(status);
    }
    //handler for custom (error_<code>)
    std::string custom_page_name (error_handler_prefix);
    custom_page_name.append(std::to_string(status));
    //find whether there is such handler
    auto r = _endpoints.find(custom_page_name);
    Handler h;
    //process results and finish with empty container or with handler
    while (!h && !r.empty()) {
        auto m = r.top();
        h = m.payload.get(req.get_method());
        if (!h) {
            h = m.payload.get(Method::not_set);
        }
    }
    //we did not find error handler
    if (!h){
        //generate own version of error page
        //xhtml page
        req.content_type(ContentType::xhtml);
        std::ostringstream text;
        text << "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
                "<html xmlns=\"http://www.w3.org/1999/xhtml\">"
                "<head>"
                "<title>" << req.get_status() << " " << req.get_status_message() <<"</title>"
                "</head>"
                "<body>"
                "<h1>"  << req.get_status() << " " << req.get_status_message() <<"</h1>"
                "</body>"
                "</html>";
        //send the stream
        return [&]{return req.send(text);};
    }
    return h.call(req, req.get_path());
}

void Router::set_handler(std::string_view path, Handler h) {
    set_handler(path, Method::not_set, h);
}

void Router::set_handler(std::string_view path, Method m, Handler h) {
    MethodMap *mm = _endpoints.find_exact(path);
    if (mm) {
        mm->set(m, std::move(h));
    } else {
        MethodMap smm;
        smm.set(m, std::move(h));
        _endpoints.insert(std::string(path), std::move(smm));
    }
}

void Router::set_handler(std::string_view path, std::initializer_list<Method> methods, Handler h) {
    MethodMap *mm = _endpoints.find_exact(path);
    if (mm) {
        for (auto x: methods) {
            mm->set(x, h);
        }
    } else {
        MethodMap smm;
        for (auto x: methods) {
            smm.set(x, h);
        }
        _endpoints.insert(std::string(path), std::move(smm));
    }
}

std::size_t Router::call_handler(ServerRequest &req, IHandler::Ret &fut) {
    return call_handler(req, req.get_path(), fut);
}

std::size_t Router::call_handler(ServerRequest &req, std::string_view vpath, IHandler::Ret &fut) {
    return call_handler(req, req.get_method(), vpath, fut);
}

std::size_t Router::call_handler(ServerRequest &req, Method method, std::string_view path, IHandler::Ret &fut) {
    if (path.empty()) return 1;
    //try to find all matching endpoint
    auto hfnd = _endpoints.find(path);
    //this variable contains bitvector for all methods (each method is 1 bit)
    //this allow to collect all available methods during traversing endpoints
    int allow_bitvector = 0;
    //process results until all removed
    while  (!hfnd.empty()) {
        //get top most endpoint
        const auto &ep = hfnd.top();
        //pop it (however it is valid operation, has the reference is still exists)
        hfnd.pop();
        //calculate vpath
        std::string_view vpath = path.substr(ep.path.length());
        //retrieve handler for given method
        Handler h = ep.payload.get(method);
        //if no handler registered
        if (!h) {
            //try to retrieve global handler
            h = ep.payload.get(Method::not_set);
            //if not set either
            if (!h) {
                //record method to bitvector
                allow_bitvector |= ep.payload.allowed_bitvector();
                //continue by next handler
                continue;
            }
        }
        //call handler
        fut << [&]{return h.call(req, vpath);};
        //explore result
        //if the result is not ready  (continued asynchronously) or is touched (modified state)
        if (!fut.ready() || !req.untouched()) {
            //we assume that handler processed or being processing the request
            //future is set, return
            return 0;
        }
        //future is ready and request is untouched, handled rejected the request
        //continue by next handler
    }
    return allow_bitvector | 1;


}

void Server::select_handler(ServerRequest &req, IHandler::Ret &fut) {
    //everything under shared lock
    std::shared_lock lk(_mx);

    std::size_t r;

    std::string_view prefix = req[strtable::hdr_x_forwarded_prefix];

    if (prefix.empty()) {
        r = call_handler(req, fut);
    } else {
        std::string_view path = req.get_path();
        if (path.substr(0, prefix.size()) == prefix) {
            r = call_handler(req, path, fut);
        } else {
            r = 1;
        }
    }

    if (r == 0) return;

    //what bitvector says - as there were handlers for different methods
    if (r > 1) {
        //in this case, response is 405 Method Not Allowed with list of allowed methods
        //generate this list
        req(strtable::hdr_allow, MethodMap::allowed_to_string(r));
        //set status
        req.set_status(405);
    } else {
        req.set_status(404);
    }
    //unlock the lock, as send_error_page is need it
    lk.unlock();
    //generate error page
    fut << [&]{return send_error_page(req);};

}



}

}
