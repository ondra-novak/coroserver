/*
 * static_page.h
 *
 *  Created on: 18. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_HTTP_STATIC_PAGE_H_
#define SRC_COROSERVER_HTTP_STATIC_PAGE_H_

#include "http_server.h"
#include <filesystem>

namespace coroserver {

namespace http {

class StaticPage {
public:

    StaticPage(std::filesystem::path document_root, std::string index_html = "index.html", unsigned int cache_seconds = 0);


    cocls::future<bool> operator()(ServerRequest &req, std::string_view vpath) const;

protected:

    std::filesystem::path _doc_root;
    std::string _index_html;
    unsigned int _cache_seconds;

};


}


}



#endif /* SRC_COROSERVER_HTTP_STATIC_PAGE_H_ */
