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
        m_server.set_message_handler(bind(&mobile_signaling_server::on_message,this,::_1,::_2));

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

        websocketpp::http::parameter_list::const_iterator it;
        for (it = extensions.begin(); it != extensions.end(); ++it) {
            if (it->first == "mobile-signaling") {
                websocketpp::http::attribute_list::const_iterator param;
                subconnection_info info;
                // Parse the negotiated parameters
                for (param = it->second.begin(); param != it->second.end(); ++param) {
                    if (param->first == "connection_id")
                        info.id = param->second;
                    else if (param->first == "primary") {
                        info.primary_connection = true;
                    }
                }
                // Use the parameters to set up subconnection
                m_con_ids[hdl] = info;
                auto it = m_connections.find(info.id);
                if (it == m_connections.end()) {
                    if (info.primary_connection) {
                        // Connection with this ID does not exist and the new link is primary
                        connection_pair connection;
                        connection.primary = hdl;
                        m_connections[info.id] = connection;
                    } else {
                        // Connection with this Id does not exist and the new link is signaling...
                        // Protocol violation!
                        // TODO: fail with an error
                        con->close(websocketpp::close::status::protocol_error, "no primary connection to signal");
                        return;
                    }
                } else {
                    if (info.primary_connection) {
                        // Adding the primary link to the connection when there is one...
                        // TODO: What happens if it is dead vs if it is alive?
                        // If it is alive, should reject? But what if we only _think_ it is alive (NAT timeout)?
                        // For now assum protocol error... Fail!
                        con->close(websocketpp::close::status::protocol_error, "tried reestablishing primary connection");
                        return;
                    } else {
                        it->second.signaling = hdl;
                    }
                    
                }
            }
        }

    }

    void on_close(connection_hdl hdl) {
        subconnection_info info = get_subconnection_from_hdl(hdl);
        auto it = m_connections.find(info.id);
        if (it == m_connections.end()) {
            // this connection is not in the list. This really shouldn't happen
            // and probably means something else is wrong.
            throw std::invalid_argument("Don't know what to close");
        }
        if (info.primary_connection) {
            std::cout << "resetting primary connection for " << info.id << "\n";
            it->second.primary.reset();
        }
        else {
            std::cout << "resetting signaling connection for " << info.id << "\n";
            it->second.signaling.reset();
        }
        if (it->second.primary.use_count() == 0 && it->second.signaling.use_count() == 0) {
            std::cout << "deleting connection " << info.id << "\n";
            m_connections.erase(info.id);
        }
        m_con_ids.erase(hdl);
    }

    void on_message(connection_hdl hdl, server::message_ptr msg) {
        // This will be a serious bottleneck... Need linear lookup of peer connections!
        subconnection_info info = get_subconnection_from_hdl(hdl);
        std::cout << "on_message called on " << (info.primary_connection?"primary":"secondary") 
            << " link with hdl: " << hdl.lock().get()
            << " and message: " << msg->get_payload()
            << std::endl;

        // if (info.type == secondary)

        // if (data.name == "") {
        //     data.name = msg->get_payload();
        //     std::cout << "Setting name of connection with sessionid "
        //               << data.sessionid << " to " << data.name << std::endl;
        // } else {
        //     std::cout << "Got a message from connection " << data.name
        //               << " with sessionid " << data.sessionid << std::endl;
        // }

        try {
            m_server.send(hdl, msg->get_payload(), msg->get_opcode());
        } catch (const websocketpp::lib::error_code& e) {
            std::cout << "Echo failed because: " << e
                      << "(" << e.message() << ")" << std::endl;
        }
    }

    subconnection_info get_subconnection_from_hdl(connection_hdl hdl) {
        auto it = m_con_ids.find(hdl);
        if (it == m_con_ids.end()) {
            // this connection is not in the list. This really shouldn't happen
            // and probably means something else is wrong.
            throw std::invalid_argument("Can't find subconnection");
        }
        return it->second;
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        m_server.run();
    }
private:
    typedef std::map<connection_hdl,subconnection_info,std::owner_less<connection_hdl>> con_map;
    typedef std::map<std::string,connection_pair> con_list;

    server m_server;
    con_map m_con_ids;
    con_list m_connections;
};

int main() {
    mobile_signaling_server server;
    server.run(9002);
}
