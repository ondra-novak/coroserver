#include "http_server.h"

namespace coroserver {

namespace http {

std::string_view Server::error_handler_prefix ( "error_");

cocls::future<void> Server::send_error_page(ServerRequest &req) {
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
        return req.send(text);
    }
    return h.call(req, req.get_path());
}

void Server::set_handler(std::string_view path, Handler h) {
    set_handler(path, Method::not_set, h);
}

void Server::set_handler(std::string_view path, Method m, Handler h) {
    std::unique_lock lk(_mx);
    MethodMap *mm = _endpoints.find_exact(path);
    if (mm) {
        mm->set(m, std::move(h));
    } else {
        MethodMap smm;
        smm.set(m, std::move(h));
        _endpoints.insert(std::string(path), std::move(smm));
    }
}

void Server::set_handler(std::string_view path, std::initializer_list<Method> methods, Handler h) {
    std::unique_lock lk(_mx);
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

void Server::set_handler(std::string_view path, std::size_t method_bitvector, Handler h) {
    std::unique_lock lk(_mx);
    MethodMap *mm = _endpoints.find_exact(path);
    if (mm) {
        for (int i = 0; i < static_cast<int>(Method::unknown); i++) {
            if (method_bitvector & (1<<i)) {
                mm->set(static_cast<Method>(i), h);
            }
        }
    } else {
        MethodMap smm;
        for (int i = 0; i < static_cast<int>(Method::unknown); i++) {
            if (method_bitvector & (1<<i)) {
                smm.set(static_cast<Method>(i), h);
            }
        }
        _endpoints.insert(std::string(path), std::move(smm));
    }

}

void Server::select_handler(ServerRequest &req, cocls::future<void> &fut) {
    //everything under shared lock
    std::shared_lock lk(_mx);
    //retrieve whole path
    auto path = req.get_path();
    //retrieve method
    auto method = req.get_method();
    //try to find all matching endpoint
    auto hfnd = _endpoints.find(path);
    //this variable contains bitvector for all methods (each method is 1 bit)
    //this allow to collect all available methods during traversing endpoints
    std::size_t allow_bitvector = 0;
    //process results until all removed
    while  (!hfnd.empty()) {
        //get top most endpoint
        auto ep = hfnd.top();
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
            return;
        }
        //future is ready and request is untouched, handled rejected the request
        //continue by next handler
    }
    //found nothing
    //if path doesn't end with '/', maybe, there is 'a directory', try to add '/'
    if (path.back() != '/'){
        std::string p ( path);
        p.push_back('/');
        //search for handler for modified path
        auto hfnd2 = _endpoints.find(p);
        //we found handler, handler can handle selected method or general method
        if (!hfnd2.empty() && (!hfnd2.top().payload.get(method) || !hfnd2.top().payload.get(Method::not_set))) {
            req.location(p);
            //Use 301 for GET, otherwise 307, as 308 is not well standardized
            req.set_status(method==Method::GET?301:307);
            //send empty page
            fut << [&]{return req.send(std::move(p));};
            return;
        }
    }
    //ok, failed to find a handler
    //what bitvector says - as there were handlers for different methods
    if (allow_bitvector) {
        //in this case, response is 405 Method Not Allowed with list of allowed methods
        //generate this list
        req(strtable::hdr_allow, MethodMap::allowed_to_string(allow_bitvector));
        //set status
        req.set_status(405);
    }
    //unlock the lock, as send_error_page is need it
    lk.unlock();
    //generate error page
    fut << [&]{return send_error_page(req);};

}


}

}
