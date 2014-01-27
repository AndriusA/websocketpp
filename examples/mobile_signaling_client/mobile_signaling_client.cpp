#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

// This header pulls in the WebSocket++ abstracted thread support that will
// select between boost::thread and std::thread based on how the build system
// is configured.
#include <websocketpp/common/thread.hpp>

#include <websocketpp/extensions/mobile_signaling/enabled.hpp>
// #include <websocketpp/extensions/mobile_signaling/disabled.hpp>

struct mobile_primary_conf : public websocketpp::config::asio_client {
    typedef mobile_primary_conf type;
    /// mobile_signaling extension
    struct m_s_conf : public type::mobile_signaling_config {
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

struct mobile_signaling_conf : public websocketpp::config::asio_client {
    typedef mobile_primary_conf type;
    /// mobile_signaling extension
    struct m_s_conf : public type::mobile_signaling_config {
        static const bool primary_connection = false;
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

// typedef websocketpp::client<mobile_signaling> client;

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
    typedef websocketpp::client<config> client_type;

    telemetry_client() : m_open(false),m_done(false) {
        // set up access channels to only log interesting things
        m_client.set_access_channels(websocketpp::log::alevel::all | websocketpp::log::alevel::debug_handshake );
        //m_client.set_access_channels(websocketpp::log::alevel::connect);
        //m_client.set_access_channels(websocketpp::log::alevel::disconnect);
        //m_client.set_access_channels(websocketpp::log::alevel::app);

        // Initialize the Asio transport policy
        m_client.init_asio();

        m_client.start_perpetual();

        // Bind the handlers we are using
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::bind;
        m_client.set_open_handler(bind(&telemetry_client::on_open,this,::_1));
        m_client.set_close_handler(bind(&telemetry_client::on_close,this,::_1));
        m_client.set_fail_handler(bind(&telemetry_client::on_fail,this,::_1));
    }

    // This method will block until the connection is complete
    void run() {
        std::string connectUri, destination;
        std::cout << "connection type primary? " << config::m_s_conf::primary_connection << std::endl;
        if (config::m_s_conf::primary_connection) {
            connectUri = config::m_s_conf::destination().str();
        } else {
            connectUri = config::m_s_conf::coordinator().str();;
        }
    	// Create a new connection to the given URI
        websocketpp::lib::error_code ec;
        typename client_type::connection_ptr con = m_client.get_connection(connectUri, ec);
        if (ec) {
        	m_client.get_alog().write(websocketpp::log::alevel::app,
                	"Get Connection Error: "+ec.message());
        	return;
        }

        // Grab a handle for this connection so we can talk to it in a thread
        // safe manor after the event loop starts.
        m_hdl = con->get_handle();

        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);

        // Create a thread to run the ASIO io_service event loop
        asio_thread = websocketpp::lib::thread(&client_type::run, &m_client);

        // Create a thread to run the telemetry loop
        telemetry_thread = websocketpp::lib::thread(&telemetry_client::telemetry_loop,this);
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection opened, starting telemetry!");

        scoped_lock guard(m_lock);
        m_open = true;

        m_client.send(m_hdl,"open",websocketpp::frame::opcode::text);
    }

    // The close handler will signal that we should stop sending telemetry
    void on_close(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection closed, stopping telemetry!");

        scoped_lock guard(m_lock);
        m_done = true;
    }

    // The fail handler will signal that we should stop sending telemetry
    void on_fail(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection failed, stopping telemetry!");

        scoped_lock guard(m_lock);
        m_done = true;
    }

    void telemetry_loop() {
        uint64_t count = 0;
        std::stringstream val;
        websocketpp::lib::error_code ec;

        while(1) {
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
            m_client.send(m_hdl,val.str(),websocketpp::frame::opcode::text,ec);

            // The most likely error that we will get is that the connection is
            // not in the right state. Usually this means we tried to send a
            // message to a connection that was closed or in the process of
            // closing. While many errors here can be easily recovered from,
            // in this simple example, we'll stop the telemetry loop.
            if (ec) {
                m_client.get_alog().write(websocketpp::log::alevel::app,
                	"Send Error: "+ec.message());
                break;
            }

            sleep(10);
        }
    }
    websocketpp::lib::thread asio_thread;
    websocketpp::lib::thread telemetry_thread;

private:
    client_type m_client;
    websocketpp::connection_hdl m_hdl;
    websocketpp::lib::mutex m_lock;

    bool m_open;
    bool m_done;
};

int main(int argc, char* argv[]) {
    telemetry_client<mobile_primary_conf> primary;
    primary.run();
    telemetry_client<mobile_signaling_conf> signaling;
    signaling.run();

    primary.asio_thread.join();
    primary.telemetry_thread.join();
    signaling.asio_thread.join();
    signaling.telemetry_thread.join();
}
