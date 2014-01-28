#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/error.hpp>

// This header pulls in the WebSocket++ abstracted thread support that will
// select between boost::thread and std::thread based on how the build system
// is configured.
#include <websocketpp/common/thread.hpp>
#include <websocketpp/extensions/mobile_signaling/enabled.hpp>

// #include <websocketpp/extensions/mobile_signaling/disabled.hpp>

// struct mobile_primary_conf : public websocketpp::config::asio_client {
//     typedef mobile_primary_conf type;
//     /// mobile_signaling extension
//     struct m_s_conf : public type::mobile_signaling_config {
//         static const websocketpp::uri coordinator() {
//             static websocketpp::uri ret("ws://localhost:9000");
//             return ret;
//         };
//         static const websocketpp::uri destination() {
//             static websocketpp::uri ret("ws://localhost:9002");
//             return ret;
//         };
//     };

//     typedef websocketpp::extensions::mobile_signaling::enabled
//         <m_s_conf> mobile_signaling_type;
// };

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

class mobile_client {
public:
    typedef websocketpp::lib::shared_ptr<mobile_client> ptr;
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;

    mobile_client(websocketpp::connection_hdl hdl) 
        : m_open(false)
        , m_done(false)
        , m_hdl(hdl)
    {
    }

    websocketpp::connection_hdl get_hdl() {
        return m_hdl;
    }

private:
    websocketpp::lib::mutex m_lock;
    bool m_open;
    bool m_done;
    websocketpp::connection_hdl m_hdl;
};


/**
 * The telemetry client connects to a WebSocket server and sends a message every
 * second containing an integer count. This example can be used as the basis for
 * programs where a client connects and pushes data for logging, stress/load
 * testing, etc.
 */
template <typename config>
class telemetry_client {
public:
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
    typedef typename client::connection_ptr connection_ptr;
    typedef typename client::message_ptr message_ptr;

    telemetry_client() : m_open(false), m_done(false) {
        // set up access channels to only log interesting things
        m_client.set_access_channels(websocketpp::log::alevel::app | websocketpp::log::alevel::devel);
        // m_client.clear_access_channels(websocketpp::log::alevel::frame_payload);
        m_client.set_access_channels(websocketpp::log::alevel::connect);
        //m_client.set_access_channels(websocketpp::log::alevel::disconnect);
        //m_client.set_access_channels(websocketpp::log::alevel::app);

        // Initialize the Asio transport policy
        m_client.init_asio();
        // Start perpetual to avoid closig it when there is no more work remaining
        m_client.start_perpetual();

        // Bind the handlers we are using
        m_client.set_close_handler(bind(&telemetry_client::on_close_primary,this,::_1));
        m_client.set_fail_handler(bind(&telemetry_client::on_fail,this,::_1));
        m_client.set_message_handler(bind(&telemetry_client::on_message_primary, this, ::_1, ::_2));

        m_client_thread.reset(new thread_type(&client::run, &m_client));

        // Create a thread to run the telemetry loop
        telemetry_thread = websocketpp::lib::thread(&telemetry_client::telemetry_loop,this);
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
        m_primary_ptr = connect_primary(connectUri);
    }

    mobile_client::ptr connect_primary(std::string const & uri) {
        
        websocketpp::lib::error_code ec;
        connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return mobile_client::ptr();
        }
        mobile_client::ptr client_ptr;
        client_ptr.reset(new mobile_client(con->get_handle()));
        m_hdl_primary = con->get_handle();

        con->set_open_handler(bind(&telemetry_client::on_open_primary,this,::_1));

        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);
        std::cout << "Return client pointer" << std::endl;
        return client_ptr;
    }

    mobile_client::ptr connect_signaling(std::string const & uri) {
        
        websocketpp::lib::error_code ec;
        connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return mobile_client::ptr();
        }
        mobile_client::ptr client_ptr;
        client_ptr.reset(new mobile_client(con->get_handle()));
        m_hdl_signaling = con->get_handle();
        // Grab a handle for this connection so we can talk to it in a thread
        // safe manor after the event loop starts.
        con->set_open_handler(bind(&telemetry_client::on_open_signaling, this, ::_1));
        con->set_close_handler(bind(&telemetry_client::on_close_signaling, this, ::_1));
        con->set_message_handler(bind(&telemetry_client::on_message_signaling, this, ::_1, ::_2));
        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);
        return client_ptr;
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection open, opening secondary" << std::endl;
        std::string connectUri = config::m_s_conf::coordinator().str();
        // m_hdl_primary = hdl;
        m_signaling_ptr = connect_signaling(connectUri);
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
        std::cout << "received primary " << msg->get_payload() << std::endl;
    }

    void on_message_signaling(connection_hdl hdl, client::message_ptr msg) {
        std::cout << "received signaling " << msg->get_payload() << std::endl;
    }

    void telemetry_loop() {
        uint64_t count = 0;
        std::stringstream val;
        websocketpp::lib::error_code ec;

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

            m_client.get_alog().write(websocketpp::log::alevel::app, val.str());

            send_message(val.str());

            // The most likely error that we will get is that the connection is
            // not in the right state. Usually this means we tried to send a
            // message to a connection that was closed or in the process of
            // closing. While many errors here can be easily recovered from,
            // in this simple example, we'll stop the telemetry loop.
            if (ec) {
                m_client.get_alog().write(websocketpp::log::alevel::app,
                	"Send Error: "+ec.message());
                continue;
            }
        }
    }

    void send_message(std::string const & msg) {

        std::cout << "Sending message downlink" << std::endl;
        // If we have a priamry link for the connection
        if (!m_hdl_primary.expired() && m_hdl_primary.lock().get()) {
            try {
                std::cout << "sending message to primary" << std::endl;
                m_client.send(m_hdl_primary, msg, websocketpp::frame::opcode::text);
            } catch (const websocketpp::lib::error_code& e) {
                std::cout << "Echo failed because: " << e
                          << "(" << e.message() << ")" << std::endl;
            }            
        } else if (!m_hdl_signaling.expired() && m_hdl_signaling.lock().get()) {
            try {
                std::cout << "sending message to signaling" << std::endl;
                m_client.send(m_hdl_signaling, msg, websocketpp::frame::opcode::text);
            } catch (const websocketpp::lib::error_code& e) {
                std::cout << "Echo failed because: " << e
                          << "(" << e.message() << ")" << std::endl;
            }
        } else {
            std::cerr << "Echo failed because both links are down" << std::endl;
        }
    }

    websocketpp::lib::thread asio_thread;
    websocketpp::lib::thread telemetry_thread;

private:
    typedef websocketpp::lib::thread thread_type;
    typedef websocketpp::lib::shared_ptr<thread_type> thread_ptr;
    typedef websocketpp::lib::shared_ptr<connection_hdl> shared_connection_ptr;

    bool m_open, m_done;

    client m_client;
    thread_ptr m_client_thread;

    websocketpp::connection_hdl m_hdl_primary, m_hdl_signaling;
    mobile_client::ptr m_primary_ptr, m_signaling_ptr;
    websocketpp::lib::mutex m_lock;
};

int main(int argc, char* argv[]) {
    telemetry_client<mobile_signaling_conf> client;
    client.connect();
    // sleep(100);
    // client.asio_thread.join();
    client.telemetry_thread.join();
}
