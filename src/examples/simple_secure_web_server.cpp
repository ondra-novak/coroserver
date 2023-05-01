#include <coroserver/io_context.h>

#include <iostream>
#include <coroserver/peername.h>
#include <coroserver/http_server.h>
#include <coroserver/http_static_page.h>
#include <coroserver/ssl_common.h>
#include <coroserver/ssl_stream.h>

using namespace coroserver;

static std::string_view localhost_privatekey = R"pem(
-----BEGIN PRIVATE KEY-----
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDG5QIf6evOBllG
KOuohbHZMFi1Mxu5bvPK6Egl3NP2kGY+ZomPf8U6k2AC3gT4U5pOX8KjkYdKBj+t
Ji75g3gknXRqJKj0VQ9aaqjGe4IRKsmwc5Z77uZAmf1YoHGD5P700o9M7o0+QVnq
NDyIANmmengeZqzLbSmUqLcq+cU0veRpNy3o/FY2pSDhkJ9IgdIbwTnxB68qvcVH
A48t8nmeV06aZRzL2/LCWXo4faUNM47BAw1bnK044il0S6bvsW/U5hafYpT1Q6BV
B3dIMxeCY1uQkqoZhrh/ZBtug3RK8YErf25RU9qaa+1NTXOe+8DmxbXnu3WhuMZM
CzZghsVbAgMBAAECggEAUjts3XUBoXTOhEt2434jQgDTLFetJsXQ1hujeMQMfuzE
2Rfb2BCjosw77fONan5mVfERsD8DCH/848Hduhu5GKpg72Go2Rwx9NgUX7vA0wg1
y1Z+6U6ktCD2tiXfyQBsyRwlU1Ft5EdwvXxLao+vbT1FXIxm9NR1VJlM4d/SwTLm
wWH1bV4Ubd7f5c+SvO4tg5jvMsuPkXW25m1dXHwepZcrW32fEWUboNs5qqpaap8j
UOaW0V9wzqeFKs2rEMwEP7yT1yZYrPYKueTUZY+3I3ie9WKciJ3gqBmRR6HGl+8S
9osi+MbdaVxp400h9R7pEGInNO1Q8As60kK81mQZQQKBgQDUBLkSQarONb+fEl5r
GIW2sjoODlO23j0mI8guAcaX3D/6yWqRzXGOp9GnLR9Exp/OrNZqeHVqzryZHA4q
/1banX+mXCZMCEUSohJIihbIizS8x6LLJA4/6A2X6NZvAajWr3pixuagLkGjdu+f
NH6xwBzMw4If3GvWcezB9osEnwKBgQDwJ1bQ6IN5h6FGz22ICsoMR2ENFobmae4Q
BQiz/oFWNzCjiAgfrNJr6G4G1rmOj6a9vEDR0yVswllgtJNxOHgDOt6BnI3XN669
no3zw+RByLNllltoVqXPE81kmOuyIPHLKKtjojfO3ZkjXkYyuO+CWKpZxN71M09n
cr6MXPlpxQKBgQDSLKhmP7CZ4NBHWYc9tT6AQKeqXWuBYUfO8jOz39DFo/HMozRA
ux8yIoyDpAhWPmwXDmEzhJwpOC3fvd8RorOv3ee3u6u/PYdzlDR5smIphU3PQjvQ
ErsJgPlQuOExg7yirauuFaxz58bry7B46yoY/O/P0JPDD9fa6m6gTM280wKBgQDC
0zdEDY0zl4uH9ZlnR1F4uqOKSZ8w5/kAuATCeRMWDXoBAMeOYtbmQc6Y77Pjarib
rlBrqL7wx45YvMXskSIThLukLIyJb5vsKugAPQg9MgQPwvXu5HRpVShIlyKHBOED
rr+z+ZMK2I12uvF6DrwHY8T3RJaYF4Mwak8ZcgfI/QKBgQCJDf16iS7R2HQd10eV
cbjM+Ida/bwES1JIK3SxN/hRzTvJRKVMlI9gdmFAXdV3TLb65vVf1xs2jXE+0fCB
vWkqcroc24/K2JdoBIrsdmENRiyeXSlYwbPwLSUlzAEvaN2R4Qe3ysxaytmD/XUp
3XMX0T5unNvE4JyxLSNJRxMnxg==
-----END PRIVATE KEY-----
)pem";


static std::string_view localhost_certificate = R"pem(
-----BEGIN CERTIFICATE-----
MIIDlTCCAn2gAwIBAgIUFSHAC6ef4OSoMqnecsA7pblNJIAwDQYJKoZIhvcNAQEL
BQAwWTELMAkGA1UEBhMCQVUxEzARBgNVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoM
GEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDESMBAGA1UEAwwJbG9jYWxob3N0MCAX
DTIzMDUwMTExMzUwN1oYDzIxMjMwNDA3MTEzNTA3WjBZMQswCQYDVQQGEwJBVTET
MBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50ZXJuZXQgV2lkZ2l0cyBQ
dHkgTHRkMRIwEAYDVQQDDAlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IB
DwAwggEKAoIBAQDG5QIf6evOBllGKOuohbHZMFi1Mxu5bvPK6Egl3NP2kGY+ZomP
f8U6k2AC3gT4U5pOX8KjkYdKBj+tJi75g3gknXRqJKj0VQ9aaqjGe4IRKsmwc5Z7
7uZAmf1YoHGD5P700o9M7o0+QVnqNDyIANmmengeZqzLbSmUqLcq+cU0veRpNy3o
/FY2pSDhkJ9IgdIbwTnxB68qvcVHA48t8nmeV06aZRzL2/LCWXo4faUNM47BAw1b
nK044il0S6bvsW/U5hafYpT1Q6BVB3dIMxeCY1uQkqoZhrh/ZBtug3RK8YErf25R
U9qaa+1NTXOe+8DmxbXnu3WhuMZMCzZghsVbAgMBAAGjUzBRMB0GA1UdDgQWBBTL
uP4jfpVfRdlP5Gg/H1T6AbEmwTAfBgNVHSMEGDAWgBTLuP4jfpVfRdlP5Gg/H1T6
AbEmwTAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQCAwrdd8adW
4mViH++7/tma3GpKkDxmkGxI8L2tVKprZaFTHo9y6A3XTdE3rIKua58XuJpo3dfJ
DPR2GtkhGA8IdPkqCoBoj1J99gCJaSyLZgzSuriZ5OQhdxhT7Mfv5DCk0NJFhSAl
v0B8ZRU8Ozaax2Tk3FqXmYc+zlMAojoOgqVbQwuSOHHti+zRdNSKKRuoNm7FfPSg
CtS5OqeoJggiWLUqzjVlpz/X76u7vpjqtWOQAvz1iIZtFbf/LDz2up+OgB/8rQ0l
dkg4RgLvEWzUg1uT4w8uvkONX4e50752fnmj+RdEha7RAmth39XZ4jNvHO4W9hOE
aQwTUrA+Mrf6
-----END CERTIFICATE-----
)pem";

cocls::generator<Stream> ssl_stream_generator(cocls::generator<Stream> source, ssl::Context ctx) {
    while (co_await source.next()) {
        Stream &s = source.value();
        try {
            Stream ssls = ssl::Stream::accept(s, ctx);
            co_yield ssls;
        } catch (std::exception &e) {
            std::cerr << "SSL Failed:" << e.what() << std::endl;
        }
    }
}

int main() {

    ssl::Certificate cert(localhost_certificate, localhost_privatekey);
    ssl::Context sslctx = ssl::Context::init_server();
    sslctx.set_certificate(cert);

    auto addrs = PeerName::lookup(":10000","");
    ContextIO ctx = ContextIO::create(1);
    http::Server server(http::Server::secure);
    server.set_handler("/", http::Method::GET, http::StaticPage("www"));
    auto fin = server.start(
            ssl::Stream::accept(ctx.accept(std::move(addrs)),std::move(sslctx),[]{
                    try { throw; } catch (std::exception &e) { std::cerr << e.what() << std::endl;}
            }),
            http::DefaultLogger([](std::string_view line){
                std::cout << line << std::endl;
            }));
    std::cout << "Press enter to stop server:" << std::endl;
    std::cin.get();
    ctx.stop();
    fin.join();
    return 0;
}

