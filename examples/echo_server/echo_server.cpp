#include <websocketpp/config/asio_no_tls.hpp>

#include <websocketpp/server.hpp>
#include <websocketpp/extensions/permessage_deflate/enabled.hpp>
#include <iostream>

struct deflate_config : public websocketpp::config::asio {
    typedef deflate_config type;
    /// permessage_compress extension
    // struct deflate_conf : type::permessage_deflate_config {
    //     static const bool no_context_takeover = true;
    // };
    typedef websocketpp::extensions::permessage_deflate::enabled
        <type::permessage_deflate_config> permessage_deflate_type;
};

typedef websocketpp::server<deflate_config> server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef server::message_ptr message_ptr;

// Define a callback to handle incoming messages
void on_message(server* s, websocketpp::connection_hdl hdl, message_ptr msg) {
    std::cout << "on_message called with hdl: " << hdl.lock().get()
              << " and message: " << msg->get_payload()
              << std::endl;

    try {
        s->send(hdl, msg->get_payload(), msg->get_opcode());
    } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Echo failed because: " << e
                  << "(" << e.message() << ")" << std::endl;
    }
}

int main() {
	// Create a server endpoint
    server echo_server;

	try {
        // Set logging settings
        echo_server.set_access_channels(websocketpp::log::alevel::all | websocketpp::log::alevel::debug_handshake | websocketpp::log::alevel::disconnect);
        //echo_server.set_access_channels(websocketpp::log::alevel::app);
        echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Initialize ASIO
        echo_server.init_asio();

        // Register our message handler
        echo_server.set_message_handler(bind(&on_message,&echo_server,::_1,::_2));

        // Listen on port 9002
        echo_server.listen(9002);

        // Start the server accept loop
        echo_server.start_accept();

	    // Start the ASIO io_service run loop
        echo_server.run();
    } catch (const std::exception & e) {
        std::cout << e.what() << std::endl;
    } catch (websocketpp::lib::error_code e) {
        std::cout << e.message() << std::endl;
    } catch (...) {
        std::cout << "other exception" << std::endl;
    }
}
