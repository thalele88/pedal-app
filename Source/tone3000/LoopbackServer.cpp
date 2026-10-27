#include "tone3000/LoopbackServer.h"

namespace pedal::tone3000
{

LoopbackServer::LoopbackServer() : juce::Thread ("TONE3000 sign-in") {}

LoopbackServer::~LoopbackServer()
{
    stop();
}

bool LoopbackServer::start()
{
    if (isRunning())
        return true;

    // Port 0 lets the OS choose a free port. Binding to 127.0.0.1 keeps the
    // receiver unreachable from the network.
    if (! listener.createListener (0, "127.0.0.1"))
        return false;

    const int bound = listener.getBoundPort();
    if (bound <= 0)
    {
        listener.close();
        return false;
    }

    port = bound;
    startThread();
    return true;
}

void LoopbackServer::stop()
{
    signalThreadShouldExit();
    listener.close(); // wakes up waitForNextConnection()
    stopThread (3000);
    port = 0;
}

juce::String LoopbackServer::getRedirectUri() const
{
    return isRunning() ? "http://127.0.0.1:" + juce::String (port.load()) + "/callback" : juce::String();
}

void LoopbackServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());
        if (connection == nullptr || threadShouldExit())
            continue;
        handleConnection (*connection);
    }
}

void LoopbackServer::handleConnection (juce::StreamingSocket& connection)
{
    // Only the request line matters: "GET /callback?code=..&state=.. HTTP/1.1"
    juce::MemoryOutputStream head;
    char buffer[1024];
    while (! head.toString().contains ("\r\n\r\n") && head.getDataSize() < 16 * 1024)
    {
        if (connection.waitUntilReady (true, 2000) != 1)
            break;
        const int n = connection.read (buffer, (int) sizeof (buffer), false);
        if (n <= 0)
            break;
        head.write (buffer, (size_t) n);
    }

    const auto requestLine = head.toString().upToFirstOccurrenceOf ("\r\n", false, false);
    const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    const auto query = target.fromFirstOccurrenceOf ("?", false, false);

    // Browsers also ask for /favicon.ico; only a real redirect counts.
    const bool isRedirect = requestLine.startsWith ("GET ") && path == "/callback" && query.contains ("state=");

    juce::String response;
    if (isRedirect)
    {
        const auto html = juce::String::fromUTF8 (
            "<!doctype html><html><head><meta charset=\"utf-8\"><title>Pedal App</title>"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
            "background:#121316;color:#e8e8ea;font:16px/1.5 -apple-system,Segoe UI,Helvetica,Arial,sans-serif}"
            "main{text-align:center;padding:32px}h1{font-size:22px;margin:0 0 8px}p{margin:0;opacity:.7}</style></head>"
            "<body><main><h1>Pronto!</h1><p>Pode fechar esta aba e voltar para o Pedal App.</p></main></body></html>");

        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/html; charset=utf-8\r\n"
                 << "Content-Length: " << (int) html.getNumBytesAsUTF8() << "\r\n"
                 << "Cache-Control: no-store\r\n"
                 << "Connection: close\r\n\r\n"
                 << html;

        if (auto callback = onCallback)
            juce::MessageManager::callAsync ([callback, query] { callback (query); });
    }
    else
    {
        response << "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }

    connection.write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
    connection.close();
}

} // namespace pedal::tone3000
