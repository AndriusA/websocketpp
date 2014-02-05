/*
 * Copyright (c) 2014, Andrius Aucinas. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/error.hpp>

// This header pulls in the WebSocket++ abstracted thread support that will
// select between boost::thread and std::thread based on how the build system
// is configured.
#include <websocketpp/common/thread.hpp>
#include <websocketpp/extensions/mobile_signaling/enabled.hpp>


struct mobile_signaling_conf : public websocketpp::config::asio_client {
    typedef mobile_signaling_conf type;
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

typedef websocketpp::client<mobile_signaling_conf> client;
typedef websocketpp::lib::shared_ptr<client> endpoint_ptr;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using websocketpp::lib::ref;
using websocketpp::lib::error_code;

class mobile_signaling_proxy {
public:
    typedef websocketpp::lib::shared_ptr<mobile_signaling_proxy> ptr;
    typedef client::connection_ptr connection_ptr;
    typedef client::message_ptr message_ptr;

    mobile_signaling_proxy(std::string connection_id, std::string const & uri, endpoint_ptr endpoint_ptr)
        : m_connection_id(connection_id)
        , m_uri(uri)
        , m_endpoint_ptr(endpoint_ptr)
    {}

    void connect(connection_hdl hdl) {
        m_hdl = hdl;
        // TODO: IMPLEMENT (add the ability to send those control messages...)
        m_endpoint_ptr->send_control(m_hdl, connection_id, blah...);
    }

    void send(std::string msg) {
        m_endpoint_ptr->send(m_hdl, msg, websocketpp::frame::opcode::text);
    }
    void on_message(connection_hdl hdl, message_ptr msg) {
        std::cout << "Connecton " << m_connection_id << " received message: " << msg->get_payload() << std::endl;
    }
    void on_open(connection_hdl hdl) {
        std::cout << "Connecton " << m_connection_id << " opened!" << std::endl;
    }

private:
    std::string const m_connection_id;
    std::string const m_uri;

    // Careful with thread-safety!
    endpoint_ptr m_endpoint_ptr;        // Shared across all proxies with the same url/all proxies
    connection_hdl m_hdl;               // Shared across all proxies with the same url/all proxies

};


//TODO: use signleton pattern!
class mobile_signaling_proxy_manager {
public:
    mobile_signaling_proxy_manager() {
        m_endpoint_ptr->set_access_channels(websocketpp::log::alevel::app | websocketpp::log::alevel::devel);
        m_endpoint_ptr->clear_access_channels(websocketpp::log::alevel::frame_payload);
        m_endpoint_ptr->set_access_channels(websocketpp::log::alevel::connect);
        m_endpoint_ptr->init_asio();
        m_endpoint_ptr->start_perpetual();
        m_endpoint_thread.reset(new thread_type(&client::run, m_endpoint_ptr.get()));


        m_endpoint_ptr->set_message_handler(bind(&mobile_signaling_proxy_manager::demultiplex_message, this, ::_1, ::_2));
        m_endpoint_ptr->set_close_handler(bind(&mobile_signaling_proxy_manager::close_mux_connection, this, ::_1));
        m_endpoint_ptr->set_fail_handler(bind(&mobile_signaling_proxy_manager::fail_mux_connection, this, ::_1));

        //TODO: IMPLEMENT
        m_endpoint_ptr->set_control_message_handler(bind(&mobile_signaling_proxy_manager::on_control_message, this, ::_1, ::_2));
    }

    mobile_signaling_proxy::ptr connect_proxy(websocketpp::http::parser::response primary_response) {
        // Connect the proxy with the given url
        // 1. Check if we already have a connection to this proxy
        // 2. If yes, we must also have a connection_hdl - reuse; otherwise, create new
        // 3. For the endpoint/connection_hdl create a new proxy with the connection_id, binding all of its methods to the id

        // TODO: extract connection_id from primary_response

        // websocketpp::http::parameter_list extensions;
        // priamry_response.get_header_as_plist("Sec-WebSocket-Extensions", extensions);
        // std::string connection_id;
        // for (auto it = extensions.begin(); it != extensions.end(); it ++) {
        //     if (it->first == "mobile-signaling") {
        //         websocketpp::http::attribute_list a = it->second;
        //         if (a.find("connection_id") != a.end()) {
        //             connection_id = a.find("connection_id")->second;
        //             connect_signaling(config::m_s_conf::coordinator().str(), connection_id);
                    
        //         }
        //     }
        // }

        std::string connection_id = "FOOBAR";
        std::string proxy_uri = "ws://localhost:9002";
        auto it = connection_hdls.find(proxy_uri);
        mobile_signaling_proxy::ptr proxy_ptr(new mobile_signaling_proxy(connection_id, proxy_uri, m_endpoint_ptr));
        connection_hdl hdl;
        if (it == connection_hdls.end()) {
            hdl = it->second;
        } else {
            error_code ec;
            connection_ptr con = m_endpoint_ptr->get_connection(m_uri, ec);
            if (ec) {
                m_endpoint_ptr->get_alog().write(websocketpp::log::alevel::app, "Get Connection Error: "+ec.message());
                // TODO error
                return mobile_signaling_proxy::ptr();
            }
            hdl = con->get_handle();
            // con->set_open_handler(bind(&mobile_signaling_client::on_open_primary,this,::_1));
            // Queue the connection. No DNS queries or network connections will be
            // made until the io_service event loop is run.
            m_endpoint_ptr->connect(con);
            connection_hdls[proxy_uri] = hdl;
        }

        proxy_ptr->connect(hdl);
        return proxy_ptr;
    }

private:
    typedef std::map<std::string, connection_hdl> conenction_hdl_map_uri;
    typedef std::map<std::string, mobile_signaling_proxy::ptr> proxy_map_id;
    typedef websocketpp::lib::thread thread_type;
    typedef websocketpp::lib::shared_ptr<thread_type> thread_ptr;

    conenction_hdl_map_uri connection_hdls;
    proxy_map_id proxies;
    endpoint_ptr m_endpoint_ptr;
    thread_ptr m_endpoint_thread;

    void demultiplex_message(connection_hdl hdl, client::message_ptr msg) {
        std::string msg_connection_id = "FOOBAR"; // msg->get_extension_payload();
        // TODO: EXPENSIVE!
        auto it = proxies[msg_connection_id];
        if (it == proxies.end()) {
            std::cerr << "Something went wrong - no proxy connection to forward the message to" << std::endl;
            return;
        }
        it->second->on_message(hdl, msg);
    }

    void close_mux_connection(connection_hdl hdl) {
        // TODO: close all multiplexed connections using the hdl
    }
    void fail_mux_connection(connection_hdl hdl) {
        // TODO: close all multiplexed connections using the hdl   
    }
    void on_control_message(connection_hdl hdl, client::message_ptr msg) {
        // If the message is connetion establishment message,
        std::string connection_id = "FOOBAR" // extract connection ID
        auto it = proxies[connection_id];
        if (it == proxies.end()) {
            std::cerr << "Something went wrong - no proxy connection to forward the message to" << std::endl;
            return;
        }
        it->second->on_open(hdl);
    }
};

template <typename config>
class mobile_signaling_client {
public:
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
    typedef typename client::connection_ptr connection_ptr;
    typedef typename client::message_ptr message_ptr;

    mobile_signaling_client() : m_open(false), m_done(false) {
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
        m_client.set_close_handler(bind(&mobile_signaling_client::on_close_primary,this,::_1));
        m_client.set_fail_handler(bind(&mobile_signaling_client::on_fail,this,::_1));
        m_client.set_message_handler(bind(&mobile_signaling_client::on_message_primary, this, ::_1, ::_2));

        m_client_thread.reset(new thread_type(&client::run, &m_client));

        // Create a thread to run the telemetry loop
        telemetry_thread = websocketpp::lib::thread(&mobile_signaling_client::telemetry_loop,this);
    }

    // This method will block until the connection is complete
    void connect() {
        // The algorithm:
        // 1. Set desination to uri, take coordinator from the config
        // 2. Set up primary connection to uri
        // 3. Once the connection is established (so, fron on_connect handler) set up connection
        //    to coordinator, destination pointing to uri
        std::cout << "Connect primary" << std::endl;

        websocketpp::lib::error_code ec;
        connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app, "Get Connection Error: "+ec.message());
            return;
        }
        m_hdl_primary = con->get_handle();
        con->set_open_handler(bind(&mobile_signaling_client::on_open_primary,this,::_1));
        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection open, opening secondary" << std::endl;
        client::connection_ptr con = m_client.get_con_from_hdl(hdl);
        websocketpp::http::parser::response resp = con->get_response();
        // Ask the manager to set up a signaling connection
        signaling.reset(signaling_manager.connect(resp));
    }

    // The close handler will signal that we should stop sending telemetry
    void on_close_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection closed!" << std::endl;
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
        std::cout << "Sending message uplink" << std::endl;
        // If we have a primary link for the connection
        if (!m_hdl_primary.expired() && m_hdl_primary.lock().get()) {
            try {
                std::cout << "sending message to primary" << std::endl;
                m_client.send(m_hdl_primary, msg+" (primary)", websocketpp::frame::opcode::text);
            } catch (const websocketpp::lib::error_code& e) {
                std::cout << "Sending failed because: " << e
                          << "(" << e.message() << ")" << std::endl;
            }            
        } else {
            // Primary is down - try sending over signaling
            // TODO: error handling
            signaling->send(msg+" (secondary)");
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
    
    websocketpp::lib::mutex m_lock;

    mobile_signaling_proxy_manager signaling_manager;
    mobile_signaling_proxy::ptr signaling;
};

int main(int argc, char* argv[]) {
    mobile_signaling_client<mobile_signaling_conf> client;
    client.connect();
    client.telemetry_thread.join();
}
