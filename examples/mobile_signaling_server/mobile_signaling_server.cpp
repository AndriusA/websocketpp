#include <iostream>
#include <map>
#include <exception>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/http/parser.hpp>
#include <websocketpp/close.hpp>

#include <websocketpp/extensions/permessage_deflate/disabled.hpp>
#include <websocketpp/extensions/mobile_signaling/enabled.hpp>

struct mobile_signaling_conf : public websocketpp::config::asio {
    typedef mobile_signaling_conf type;
    struct m_s_conf : public type::mobile_signaling_config {
        static websocketpp::uri coordinator() {
            static websocketpp::uri ret("ws://localhost:9000");
            return ret;
        }
    };
    typedef websocketpp::extensions::mobile_signaling::enabled
        <m_s_conf> mobile_signaling_type;
};


typedef websocketpp::server<mobile_signaling_conf> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using websocketpp::lib::error_code;

struct subconnection_info {
    std::string id;
    bool primary_connection = false;
};
struct connection_pair {
    connection_hdl primary, signaling;
};

class mobile_signaling_server {
public:
    mobile_signaling_server() {
        m_server.init_asio();

        m_server.set_open_handler(bind(&mobile_signaling_server::on_open,this,::_1));
        m_server.set_close_handler(bind(&mobile_signaling_server::on_close,this,::_1));
        // m_server.set_message_handler(bind(&mobile_signaling_server::on_message,this,::_1,::_2));

        m_server.clear_access_channels(websocketpp::log::alevel::frame_payload);
        m_server.set_validate_handler(bind(&mobile_signaling_server::validate,this,::_1));
    }

    bool validate(connection_hdl hdl) {
        server::connection_ptr con = m_server.get_con_from_hdl(hdl);

        server::connection_type::response_type response;
        response = con->get_response();
        websocketpp::http::parameter_list extensions;
        bool error = response.get_header_as_plist("Sec-WebSocket-Extensions", extensions);
        if (error)
            return error;

        websocketpp::http::parameter_list::const_iterator it;
        for (it = extensions.begin(); it != extensions.end(); ++it) {
            if (it->first == "mobile-signaling") {
                websocketpp::http::attribute_list::const_iterator param;
                bool primary = false;
                std::string id;
                // Parse the negotiated parameters
                for (param = it->second.begin(); param != it->second.end(); ++param) {
                    if (param->first == "connection_id")
                        id = param->second;
                    else if (param->first == "primary") {
                        primary = true;
                    }
                }                
                auto it = m_connections.find(id);
                if (it == m_connections.end() && !primary) {
                    // No primary connection to set up signaling for
                    return false;
                } else if (it != m_connections.end() && primary && it->second.primary.use_count() > 0) {
                    // Trying to establish primary connection while primary exists
                    return false;
                }
            }
        }

        return true;
    }


    /// Associate primary and signaling (signaling) links
    /**
     * 
     * Associates two separate websocket links based on the negotiated header fields:
     * connection_id - connection id negotiated between the client and the server, the same for both links
     * connectionType - primary/signaling for the two links
     *
     * @since mobile_signaling_extension
     */
    void on_open(connection_hdl hdl) {
        server::connection_ptr con = m_server.get_con_from_hdl(hdl);

        std::cout << "Sec-WebSocket-Extensions: " << con->get_response_header("Sec-WebSocket-Extensions") << std::endl;
        server::connection_type::response_type response;
        response = con->get_response();

        websocketpp::http::parameter_list extensions;
        bool error = response.get_header_as_plist("Sec-WebSocket-Extensions", extensions);

        if (error) {
            // really shouldn't happen after successful open
            return;
        }

        bool primary_connection = false;
        std::string id;
        for (websocketpp::http::parameter_list::const_iterator it = extensions.begin(); it != extensions.end(); ++it) {
            if (it->first == "mobile-signaling") {
                websocketpp::http::attribute_list::const_iterator param;
                // Parse the negotiated parameters
                for (param = it->second.begin(); param != it->second.end(); ++param) {
                    if (param->first == "connection_id")
                        id = param->second;
                    else if (param->first == "primary") {
                        primary_connection = true;
                    }
                }
            }
        }

        auto it = m_connections.find(id);
        // Close the new connection if we have a protocol error
        if (it == m_connections.end() && !primary_connection) {
            // Want to set up a signaling connection for inexsiting primary con
            con->close(websocketpp::close::status::protocol_error, "no primary connection to signal");
            return;
        }
        if (it != m_connections.end() && primary_connection && !it->second.primary.expired()) {
            // Want to set up a primary connection while one already exists
            con->close(websocketpp::close::status::protocol_error, "primary link exists");
            return;
        }

        // Set up connection pairs in the map
        if (it == m_connections.end() && primary_connection) {
            // Connection with this ID does not exist and the new link is primary
            connection_pair connection;
            connection.primary = hdl;
            std::pair<con_list::iterator, bool> ret = m_connections.insert(std::pair<std::string, connection_pair>(id, connection));
            if (!ret.second) {
                std::cerr << "Connection not inserted, key already existed!" << std::endl;
            }
            it = ret.first;            
        } else if (primary_connection && it->second.primary.expired()) {
            // Adding the primary link to the connection after it has died
            it->second.primary = hdl;            
        } else {
            it->second.signaling = hdl;
        }
        
        // Set up the various handlers for the pair of connections
        if (primary_connection){
            con->set_close_handler(bind(&mobile_signaling_server::on_close_primary, this, ::_1, id));
            con->set_message_handler(bind(&mobile_signaling_server::on_message_primary, this, ::_1, it->second.signaling, ::_2));
            if (!it->second.signaling.expired()) {
                server::connection_ptr signaling_con = m_server.get_con_from_hdl(it->second.signaling);
                signaling_con->set_message_handler(bind(&mobile_signaling_server::on_message_signaling, this, ::_1, it->second.primary, ::_2));
            }
        }
        else {
            con->set_close_handler(bind(&mobile_signaling_server::on_close_signaling, this, ::_1, id));
            // Bind message handler to use the new connection handler and previously established primary
            con->set_message_handler(bind(&mobile_signaling_server::on_message_signaling, this, it->second.primary, ::_1, ::_2));
            server::connection_ptr primary_con = m_server.get_con_from_hdl(it->second.primary);
            primary_con->set_message_handler(bind(&mobile_signaling_server::on_message_primary, this, ::_1, it->second.signaling, ::_2));
        }

        std::cout << "Opened " << (primary_connection?"primary":"secondary") << " connection, hdl " << hdl.lock().get() << std::endl;
    }

    // Simple on_close method - only called if closed before primary and signaling links are setup
    void on_close(connection_hdl hdl) {
        std::cout << "Connection " << hdl.lock().get() << " closed before setup finished" << std::endl;
    }

    void on_close_primary(connection_hdl hdl, std::string connection_id) {
        std::cout << "Closed primary connection for " << connection_id << "\n";
        auto it = m_connections.find(connection_id);
        if (it == m_connections.end()) {
            std::cerr << "Don't know what to close!" << std::endl;
            return;
        }
        error_code ec;
        server::connection_ptr con = m_server.get_con_from_hdl(it->second.signaling, ec);
        if (ec) {
            std::cout << "signaling also dead, deleting connection " << connection_id << "\n";
            m_connections.erase(connection_id);
            return;
        }
        websocketpp::session::state::value state = con->get_state();
        if (state == websocketpp::session::state::closing || state == websocketpp::session::state::closed) {
            std::cout << "signaling also dead, deleting connection " << connection_id << "\n";
            m_connections.erase(connection_id);
        }
    }

    void on_close_signaling(connection_hdl hdl, std::string connection_id) {
        std::cout << "Closed signaling connection for " << connection_id << "\n";
        auto it = m_connections.find(connection_id);
        if (it == m_connections.end()) {
            std::cerr << "Don't know what to close!" << std::endl;
            return;
        }
        error_code ec;
        server::connection_ptr con = m_server.get_con_from_hdl(it->second.primary, ec);
        if (ec) {
            std::cout << "primary also dead, deleting connection " << connection_id << "\n";
            m_connections.erase(connection_id);
            return;
        }
        websocketpp::session::state::value state = con->get_state();
        if (state == websocketpp::session::state::closing || state == websocketpp::session::state::closed) {
            std::cout << "primary also dead, deleting connection " << connection_id << "\n";
            m_connections.erase(connection_id);
        }
    }

    /// on_message can be called from either connection handler and will reply on either,
    /// With priority given to the primary connection
    void on_message_primary(connection_hdl primary, connection_hdl signaling, server::message_ptr msg) {
        std::cout << "Message arrived on primary: " << msg->get_payload() << " ext " << msg->get_extension_data() << std::endl;
        server::message_ptr response = generateResponse(msg);
        send_message_downlink(primary, signaling, response);
    }

    void on_message_signaling(connection_hdl primary, connection_hdl signaling, server::message_ptr msg) {
        std::cout << "Message arrived on signaling: " << msg->get_payload() << " ext " << msg->get_extension_data() << std::endl;
        server::message_ptr response = generateResponse(msg);
        send_message_downlink(primary, signaling, response);
    }

    void send_message_downlink(connection_hdl primary, connection_hdl signaling, server::message_ptr msg) {
        std::cout << "Sending message downlink" << std::endl;
        // If we have a priamry link for the connection
        if (!primary.expired() && primary.lock().get()) {
            try {
                std::cout << "sending response to primary" << std::endl;
                m_server.send(primary, msg->get_payload(), msg->get_opcode());
            } catch (const websocketpp::lib::error_code& e) {
                std::cout << "Echo failed because: " << e
                          << "(" << e.message() << ")" << std::endl;
            }            
        } else if (!signaling.expired() && signaling.lock().get()) {
            try {
                std::cout << "sending response to signaling" << std::endl;
                m_server.send(signaling, msg->get_payload(), msg->get_opcode());
            } catch (const websocketpp::lib::error_code& e) {
                std::cout << "Echo failed because: " << e
                          << "(" << e.message() << ")" << std::endl;
            }
        } else {
            std::cerr << "Echo failed because both links are down" << std::endl;
        }
    }

    /// Generate response to an incoming message - at this point an echo
    server::message_ptr generateResponse(server::message_ptr msg) {
        return msg;
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        m_server.run();
    }
private:
    typedef std::map<std::string,connection_pair> con_list;

    server m_server;
    con_list m_connections;
};

int main() {
    mobile_signaling_server server;
    server.run(9002);
}
