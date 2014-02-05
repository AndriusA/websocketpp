#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/error.hpp>

// This header pulls in the WebSocket++ abstracted thread support that will
// select between boost::thread and std::thread based on how the build system
// is configured.
#include <websocketpp/common/thread.hpp>
#include <websocketpp/extensions/mobile_signaling/enabled.hpp>


struct mobile_signaling_conf : public websocketpp::config::asio {
    typedef mobile_signaling_conf type;
    struct m_s_conf : public type::mobile_signaling_config {
        static const bool primary_connection = false;
        static websocketpp::uri coordinator() {
            static websocketpp::uri ret("ws://localhost:9000");
            return ret;
        };
        static const websocketpp::uri destination() {
            static websocketpp::uri ret("");
            return ret;
        };
    };
    typedef websocketpp::extensions::mobile_signaling::enabled
        <m_s_conf> mobile_signaling_type;
};

typedef websocketpp::server<mobile_signaling_conf> server;
typedef websocketpp::client<mobile_signaling_conf> client;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using websocketpp::lib::ref;
using websocketpp::lib::error_code;

class proxy_client {
public:
    typedef websocketpp::lib::shared_ptr<proxy_client> ptr;
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;

    proxy_client(websocketpp::connection_hdl hdl) 
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

class signaling_proxy_server {
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
public:
    signaling_proxy_server() {
        m_server.init_asio();
        m_server.start_perpetual();
        m_server.clear_access_channels(websocketpp::log::alevel::frame_payload);
        m_server.set_validate_handler(bind(&signaling_proxy_server::validate,this,::_1));

        // m_server.set_open_handler(bind(&signaling_proxy_server::on_open,this,::_1));
        // m_server.set_close_handler(bind(&signaling_proxy_server::on_close,this,::_1));
        // m_server.set_message_handler(bind(&signaling_proxy_server::on_message,this,::_1,::_2));

        m_client.clear_access_channels(websocketpp::log::alevel::all);
        m_client.clear_error_channels(websocketpp::log::elevel::all);
        m_client.init_asio();
        m_client.start_perpetual();
        m_client_thread.reset(new thread_type(&client::run, &m_client));
        
    }

    ~signaling_proxy_server() {
        std::cout << "Proxy server destructor" << std::endl;
    }

    void on_close_in(connection_hdl in, connection_hdl out) {
        std::cout << in.lock().get() << " has closed, closing " << out.lock().get() << std::endl;
        // When incoming connection is closed, close outgoing connection as well
        error_code ec;
        client::connection_ptr in_con = m_server.get_con_from_hdl(in, ec);
        if (ec)
            return;
        client::connection_ptr out_con = m_client.get_con_from_hdl(out, ec);
        if (ec)
            return;
        websocketpp::session::state::value state = out_con->get_state();
        if (state != websocketpp::session::state::closing && state != websocketpp::session::state::closed) {
            m_client.close(out, in_con->get_remote_close_code(), "remote destination has gone away");
        }
    }

    void on_close_out(connection_hdl in, connection_hdl out) {
        // When ougoing connection is closed, close incoming connection
        std::cout << out.lock().get() << " has closed, closing " << in.lock().get() << std::endl;
        error_code ec;
        client::connection_ptr out_con = m_client.get_con_from_hdl(out, ec);
        if (ec)
            return;
        client::connection_ptr in_con = m_server.get_con_from_hdl(in, ec);
        if (ec)
            return;
        websocketpp::session::state::value state = in_con->get_state();
        if (state != websocketpp::session::state::closing && state != websocketpp::session::state::closed) {
            m_server.close(in, out_con->get_remote_close_code(), "remote destination has gone away");
        }
    }

    void on_fail_out(connection_hdl in, connection_hdl out) {
        // When ougoing connection fails, close incoming connection
        std::cout << "outgoing has failed, closing incoming" << std::endl;
        error_code ec;
        
        if (in.lock()) {
            client::connection_ptr in_con = m_server.get_con_from_hdl(in, ec);
            if (ec)
                return;
            websocketpp::session::state::value state = in_con->get_state();
            if (state != websocketpp::session::state::closing && state != websocketpp::session::state::closed) {
                try {
                    m_server.close(in, websocketpp::close::status::protocol_error, "outgoing connection has failed");
                } catch (const error_code &e) {
                    std::cerr << "Incoming conneciton close failed because: " << e
                        << "(" << e.message() << ")" << std::endl;
                }
            }
        }
    }

    void on_fail_in(connection_hdl in, connection_hdl out) {
        // When ougoing connection fails, close incoming connection
        std::cout << "incoming has failed, closing outgoing" << std::endl;
        error_code ec;
        if (out.lock()) {
            client::connection_ptr out_con = m_client.get_con_from_hdl(out, ec);
            if (ec)
                return;
            websocketpp::session::state::value state = out_con->get_state();
            if (state != websocketpp::session::state::closing && state != websocketpp::session::state::closed) {
                std::cout << "try closing..." << std::endl;
                m_client.close(out, websocketpp::close::status::protocol_error, "outgoing connection has failed");
            }    
        }
    }

    void on_message_in(connection_hdl in, connection_hdl out, server::message_ptr msg) {
        std::cout << "forwarding uplink " << msg->get_payload() << std::endl;
        // forward messages from the incoming link onto the outgoing
        try {
            m_client.send(out, msg->get_payload(), msg->get_opcode());
        } catch (const error_code& e) {
            // TODO: buffer messages for resending when the link goes back up?
            std::cout << "Forwarding in->out failed because: " << e
                      << "(" << e.message() << ")" << std::endl;
        }
    }

    void on_message_out(connection_hdl in, connection_hdl out, server::message_ptr msg) {
        std::cout << "forwarding downlink " << msg->get_payload() << std::endl;
        // forward messages from the outging link onto the incoming
        try {
            m_server.send(in, msg->get_payload(), msg->get_opcode());
        } catch (const error_code& e) {
            // TODO: buffer messages for resending when the link goes back up?
            std::cout << "Echo failed because: " << e
                      << "(" << e.message() << ")" << std::endl;
        }
    }

    void shutdown() {
        std::cout << "shutting down proxy..." << std::endl;
        // Close each connection
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            m_client.close(it->second->get_hdl(), websocketpp::close::status::going_away, "proxy shutting down");
        }
        // Stop perpetual client
        m_client.stop_perpetual();
        // Block until everythig is done
        m_client_thread->join();
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        m_server.run();
    }
private:
    typedef std::map<connection_hdl, proxy_client::ptr, std::owner_less<connection_hdl>> client_map;
    typedef websocketpp::lib::thread thread_type;
    typedef websocketpp::lib::shared_ptr<thread_type> thread_ptr;

    server m_server;
    client m_client;
    thread_ptr m_client_thread;
    client_map m_clients;
    websocketpp::lib::mutex m_lock;

    proxy_client::ptr connect(std::string const & uri, std::string connection_id, connection_hdl in_hdl) {
        websocketpp::lib::error_code ec;

        std::cout << "Connecting connection id " << connection_id << std::endl;
        // connect to this address
        client::connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            return proxy_client::ptr();
        }
       
        proxy_client::ptr p_client_ptr;
        p_client_ptr.reset(new proxy_client(con->get_handle()));

        // con->set_open_handler(bind(&proxy_client::on_open, client, ::_1));
        con->set_fail_handler(bind(&signaling_proxy_server::on_fail_out, this, in_hdl, ::_1));
        con->set_message_handler(bind(&signaling_proxy_server::on_message_out, this, in_hdl, ::_1, ::_2));
        con->set_close_handler(bind(&signaling_proxy_server::on_close_out, this, in_hdl, ::_1));

        con->replace_header("Sec-WebSocket-Extensions", "mobile-signaling; connection_id=\""+connection_id+"\"");
        std::cout << "CONNECT! Header?" << con->get_request_header("Sec-WebSocket-Extensions") << std::endl;
        m_client.connect(con);
        return p_client_ptr;
    }

    bool validate(connection_hdl in_hdl) {
        server::connection_ptr in_con = m_server.get_con_from_hdl(in_hdl);

        server::connection_type::response_type response;
        response = in_con->get_response();
        websocketpp::http::parameter_list extensions;
        bool error = response.get_header_as_plist("Sec-WebSocket-Extensions", extensions);
        if (error)
            return error;

        // Find if the client connection is signaling
        std::string destination;
        websocketpp::http::parameter_list::const_iterator it;
        std::string in_con_id;
        for (it = extensions.begin(); it != extensions.end(); ++it) {
            if (it->first == "mobile-signaling") {
                websocketpp::http::attribute_list::const_iterator param;
                bool primary = false;
                // Parse the negotiated parameters
                for (param = it->second.begin(); param != it->second.end(); ++param) {
                    if (param->first == "primary")
                        primary = true;
                    else if (param->first == "destination") {
                        destination = param->second;
                    }
                    else if (param->first == "connection_id") {
                        in_con_id = param->second;
                    }
                }                
                // We only accept singaling connections at the proxy
                if (primary)
                    return false;
            }
        }

        m_clients[in_hdl] = connect(destination, in_con_id, in_hdl);
        
        // Set up connection to the destination - fail validation if connection setup failed
        // TODO
        // std::cout << "Setting proxy message handlers" << std::endl;
        connection_hdl out_hdl = m_clients[in_hdl]->get_hdl();
        in_con->set_message_handler(bind(&signaling_proxy_server::on_message_in, this, ::_1, out_hdl, ::_2));
        in_con->set_close_handler(bind(&signaling_proxy_server::on_close_in, this, ::_1, out_hdl));
        in_con->set_close_handler(bind(&signaling_proxy_server::on_fail_in, this, ::_1, out_hdl));
        // client_con->set_message_handler(bind(&signaling_proxy_server::on_message_out, this, out_hdl, ::_1, ::_2));

        return true;
    }
};

int main(int argc, char* argv[]) {
    signaling_proxy_server server;
    server.run(9000);
    // atexit(server.shutdown);
}
