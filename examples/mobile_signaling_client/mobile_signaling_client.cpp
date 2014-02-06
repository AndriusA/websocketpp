#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/error.hpp>

// This header pulls in the WebSocket++ abstracted thread support that will
// select between boost::thread and std::thread based on how the build system
// is configured.
#include <websocketpp/common/thread.hpp>
#include <websocketpp/message_buffer/alloc.hpp>
#include <websocketpp/extensions/mobile_signaling/enabled.hpp>

struct mobile_signaling_conf : public websocketpp::config::asio_client {
    typedef mobile_signaling_conf type;

    /// mobile_signaling extension
    struct m_s_conf : public type::mobile_signaling_config {
        // Should be changed by the client depending on which connection it is set
        // static const bool primary_connection = true;
        static const websocketpp::uri coordinator() {
            static websocketpp::uri ret("ws://localhost:9000");
            return ret;
        };
        static const websocketpp::uri destination() {
            static websocketpp::uri ret("ws://localhost:9002");
            return ret;
        };
    };

    typedef websocketpp::extensions::mobile_signaling::enabled
        <m_s_conf> mobile_signaling_type;
};

typedef websocketpp::client<mobile_signaling_conf> client;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using websocketpp::lib::ref;
using websocketpp::lib::error_code;

/**
 * The telemetry client connects to a WebSocket server and sends a message every
 * second containing an integer count. This example can be used as the basis for
 * programs where a client connects and pushes data for logging, stress/load
 * testing, etc.
 */
template <typename config>
class mobile_signaling_client {
public:
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
    typedef typename client::connection_ptr connection_ptr;
    typedef typename client::message_ptr message_ptr;

    typedef websocketpp::lib::function<void(std::string)> message_handler;

    mobile_signaling_client() : m_open(false), m_done(false) {
        // set up access channels to only log interesting things
        m_client.clear_access_channels(websocketpp::log::alevel::all);
        m_client.set_access_channels(websocketpp::log::alevel::connect);
        // Initialize the Asio transport policy
        m_client.init_asio();
        // Start perpetual to avoid closig it when there is no more work remaining
        m_client.start_perpetual();

        // Bind the handlers we are using
        m_client.set_close_handler(bind(&mobile_signaling_client::on_close_primary,this,::_1));
        m_client.set_fail_handler(bind(&mobile_signaling_client::on_fail,this,::_1));
        m_client.set_message_handler(bind(&mobile_signaling_client::on_message_primary, this, ::_1, ::_2));
        m_client_thread.reset(new thread_type(&client::run, &m_client));

        set_message_handler(bind(&mobile_signaling_client::simple_message_handler, this, ::_1));
    }

    // This method will block until the connection is complete
    void connect() {
        // The algorithm:
        // 1. Set desination to uri, take coordinator from the config
        // 2. Set up primary connection to uri
        // 3. Once the connection is established (so, fron on_connect handler) set up connection
        //    to coordinator, destination pointing to uri
        std::string connectUri = config::m_s_conf::destination().str();
        std::cout << "Connect primary " << connectUri;
        connect_primary(connectUri);
    }

    void set_message_handler(message_handler h) {
        m_on_message_handler = h;
    }

    void close() {
        // TODO
    }

    error_code send(std::string const & msg) {
        std::cout << "Sending message downlink" << std::endl;
        // If we have a priamry link for the connection
        error_code ec;
        connection_ptr primary_con = m_client.get_con_from_hdl(m_hdl_primary, ec);
        if (!ec && primary_con->get_state() == websocketpp::session::state::open) {
            std::cout << "sending message to primary" << std::endl;
            ec = primary_con->send(msg, websocketpp::frame::opcode::text);
            if (!ec)
                return ec;
        }
        connection_ptr signaling_con = m_client.get_con_from_hdl(m_hdl_signaling, ec);
        if (!ec && signaling_con->get_state() == websocketpp::session::state::open) {
            std::cout << "sending message to signaling" << std::endl;
            ec = signaling_con->send(msg, websocketpp::frame::opcode::text);
            if (!ec)
                return ec;
        } 
        return ec;
    }

    websocketpp::lib::thread asio_thread;

private:
    typedef websocketpp::lib::thread thread_type;
    typedef websocketpp::lib::shared_ptr<thread_type> thread_ptr;
    typedef websocketpp::lib::shared_ptr<connection_hdl> shared_connection_ptr;

    bool m_open, m_done;

    client m_client;
    thread_ptr m_client_thread;

    websocketpp::connection_hdl m_hdl_primary, m_hdl_signaling;
    websocketpp::lib::mutex m_lock;

    message_handler m_on_message_handler;

    std::string get_response_connection_id(websocketpp::http::parser::response const & resp) {
        websocketpp::http::parameter_list extensions;
        resp.get_header_as_plist("Sec-WebSocket-Extensions", extensions);
        std::string connection_id;
        for (auto it = extensions.begin(); it != extensions.end(); it ++) {
            if (it->first == "mobile-signaling") {
                websocketpp::http::attribute_list a = it->second;
                if (a.find("connection_id") != a.end()) {
                    connection_id = a.find("connection_id")->second;
                }
            }
        }
        return connection_id;
    }

    void connect_primary(std::string const & uri) {
        websocketpp::lib::error_code ec;
        connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return;
        }
        m_hdl_primary = con->get_handle();

        con->set_open_handler(bind(&mobile_signaling_client::on_open_primary,this,::_1));

        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);
        std::cout << "Return client pointer" << std::endl;
    }

    void connect_signaling(std::string const & uri, std::string connection_id) {
        websocketpp::lib::error_code ec;
        connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return;
        }
        m_hdl_signaling = con->get_handle();

        con->replace_header("Sec-WebSocket-Extensions", "mobile-signaling; connection_id=\""+connection_id+"\"");
        con->set_open_handler(bind(&mobile_signaling_client::on_open_signaling, this, ::_1));
        con->set_close_handler(bind(&mobile_signaling_client::on_close_signaling, this, ::_1));
        con->set_message_handler(bind(&mobile_signaling_client::on_message_signaling, this, ::_1, ::_2));
        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection open, opening secondary" << std::endl;
        std::string connectUri = config::m_s_conf::coordinator().str();
        m_hdl_primary = hdl;

        // Use the same connection_id generated on establishing primary
        client::connection_ptr con = m_client.get_con_from_hdl(hdl);
        websocketpp::http::parser::response resp = con->get_response();
        std::string connection_id = get_response_connection_id(resp);
        connect_signaling(config::m_s_conf::coordinator().str(), connection_id);
        scoped_lock guard(m_lock);
        m_open = true;
    }

    void on_open_signaling(websocketpp::connection_hdl hdl) {
        std::cout << "Signaling connection open" << std::endl;
        // m_hdl_signaling = hdl;
        scoped_lock guard(m_lock);
        m_open = true;
    }

    // The close handler will signal that we should stop sending telemetry
    void on_close_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection closed!" << std::endl;
        // TODO: if signaling alive, schedule reconnect of primary
    }
    // The close handler will signal that we should stop sending telemetry
    void on_close_signaling(websocketpp::connection_hdl hdl) {
        std::cout << "Signaling connection closed!" << std::endl;
        // TODO: if signaling alive, schedule reconnect of primary
    }

    // The fail handler will signal that we should stop sending telemetry
    void on_fail(websocketpp::connection_hdl hdl) {
        std::cout << "Connection failed, stopping telemetry!" << std::endl;
        // TODO: if signaling alive, schedule reconnect of primary
    }

    void on_message_primary(connection_hdl hdl, client::message_ptr msg) {
        std::cout << "received primary " << msg->get_payload() << " ext " << msg->get_extension_data() << std::endl;
        if (m_on_message_handler) {
            m_on_message_handler(msg->get_payload());
        }
    }

    void on_message_signaling(connection_hdl hdl, client::message_ptr msg) {
        std::cout << "received signaling " << msg->get_payload() << " ext " << msg->get_extension_data() << std::endl;
        if (m_on_message_handler) {
            m_on_message_handler(msg->get_payload());
        }
    }

    void simple_message_handler(std::string msg) {
        std::cout << "received message " << msg << std::endl;
    }
};

template <typename config>
class websocket_client
{
public:
    websocket_client()
        : m_mobile_signaling_client()
        , m_open(true)
        , m_done(false)
    {
        // Create a thread to run the telemetry loop
        telemetry_thread = websocketpp::lib::thread(&websocket_client::telemetry_loop,this);
    }

    void telemetry_loop() {
        uint64_t count = 0;
        std::stringstream val;
        websocketpp::lib::error_code ec;
        m_mobile_signaling_client.connect();
        // set_message_handler(bind(&mobile_signaling_client::simple_message_handler, this, ::_1));
        m_mobile_signaling_client.set_message_handler(bind(&websocket_client::on_message, this, ::_1));

        while(1) {
            sleep(10);
            bool wait = false;

            {
                scoped_lock guard(m_lock);
                // If the connection has been closed, stop generating telemetry
                if (m_done) {break;}

                // If the connection hasn't been opened yet wait a bit and retry
                if (!m_open) {
                    wait = true;
                }
            }

            if (wait) {
                sleep(1);
                continue;
            }

            val.str("");
            val << "client: " << count++;
            if (config::m_s_conf::primary_connection)
                val << " (primary)";
            else
                val << " (signaling)";

            m_mobile_signaling_client.send(val.str());

            // The most likely error that we will get is that the connection is
            // not in the right state. Usually this means we tried to send a
            // message to a connection that was closed or in the process of
            // closing. While many errors here can be easily recovered from,
            // in this simple example, we'll stop the telemetry loop.
            if (ec) {
                std::cerr << "Send Error: " << ec.message() << std::endl;
                break;
            }
        }
    }

    void on_message(std::string msg){
        std::cout << "Client received message" << msg << std::endl;
    }

    websocketpp::lib::thread telemetry_thread;

private:
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;

    mobile_signaling_client<config> m_mobile_signaling_client;
    websocketpp::lib::mutex m_lock;
    bool m_open, m_done;
    
};

int main(int argc, char* argv[]) {
    websocket_client<mobile_signaling_conf> client;
    // client.connect();
    // sleep(100);
    // client.asio_thread.join();
    client.telemetry_thread.join();
}
