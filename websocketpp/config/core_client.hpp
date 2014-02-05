/*
 * Copyright (c) 2013, Peter Thorson. All rights reserved.
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

#ifndef WEBSOCKETPP_CONFIG_CORE_CLIENT_HPP
#define WEBSOCKETPP_CONFIG_CORE_CLIENT_HPP

// Non-Policy common stuff
#include <websocketpp/common/platforms.hpp>
#include <websocketpp/common/cpp11.hpp>
#include <websocketpp/common/stdint.hpp>

// Concurrency
#include <websocketpp/concurrency/basic.hpp>

// Transport
#include <websocketpp/transport/iostream/endpoint.hpp>

// HTTP
#include <websocketpp/http/request.hpp>
#include <websocketpp/http/response.hpp>

// Messages
#include <websocketpp/message_buffer/message.hpp>
#include <websocketpp/message_buffer/alloc.hpp>

// Loggers
#include <websocketpp/logger/basic.hpp>

// RNG
#include <websocketpp/random/random_device.hpp>

// User stub base classes
#include <websocketpp/endpoint_base.hpp>
#include <websocketpp/connection_base.hpp>

// Extensions
#include <websocketpp/extensions/permessage_deflate/disabled.hpp>
#include <websocketpp/extensions/mobile_signaling/disabled.hpp>

namespace websocketpp {
namespace config {

/// Client config with iostream transport
struct core_client {
    typedef core_client type;

    // Concurrency policy
    typedef websocketpp::concurrency::basic concurrency_type;

    // HTTP Parser Policies
    typedef http::parser::request request_type;
    typedef http::parser::response response_type;

    // Message Policies
    typedef message_buffer::message<message_buffer::alloc::con_msg_manager>
        message_type;
    typedef message_buffer::alloc::con_msg_manager<message_type>
        con_msg_manager_type;
    typedef message_buffer::alloc::endpoint_msg_manager<con_msg_manager_type>
        endpoint_msg_manager_type;

    /// Logging policies
    typedef websocketpp::log::basic<concurrency_type,
        websocketpp::log::elevel> elog_type;
    typedef websocketpp::log::basic<concurrency_type,
        websocketpp::log::alevel> alog_type;

    /// RNG policies
    typedef websocketpp::random::random_device::int_generator<uint32_t,
        concurrency_type> rng_type;

    /// Controls compile time enabling/disabling of thread syncronization code
    /// Disabling can provide a minor performance improvement to single threaded
    /// applications
    static bool const enable_multithreading = true;

    struct transport_config {
        typedef type::concurrency_type concurrency_type;
        typedef type::elog_type elog_type;
        typedef type::alog_type alog_type;
        typedef type::request_type request_type;
        typedef type::response_type response_type;

        /// Controls compile time enabling/disabling of thread syncronization
        /// code Disabling can provide a minor performance improvement to single
        /// threaded applications
        static bool const enable_multithreading = true;

        /// Default timer values (in ms)

        /// Length of time to wait for socket pre-initialization
        /**
         * Exactly what this includes depends on the socket policy in use
         */
        static const long timeout_socket_pre_init = 5000;

        /// Length of time to wait before a proxy handshake is aborted
        static const long timeout_proxy = 5000;

        /// Length of time to wait for socket post-initialization
        /**
         * Exactly what this includes depends on the socket policy in use.
         * Often this means the TLS handshake
         */
        static const long timeout_socket_post_init = 5000;

        /// Length of time to wait for dns resolution
        static const long timeout_dns_resolve = 5000;

        /// Length of time to wait for TCP connect
        static const long timeout_connect = 5000;

        /// Length of time to wait for socket shutdown
        static const long timeout_socket_shutdown = 5000;
    };

    /// Transport Endpoint Component
    typedef websocketpp::transport::iostream::endpoint<transport_config>
        transport_type;

    /// User overridable Endpoint base class
    typedef websocketpp::endpoint_base endpoint_base;
    /// User overridable Connection base class
    typedef websocketpp::connection_base connection_base;

    /// Default timer values (in ms)

    /// Length of time before an opening handshake is aborted
    static const long timeout_open_handshake = 5000;
    /// Length of time before a closing handshake is aborted
    static const long timeout_close_handshake = 5000;
    /// Length of time to wait for a pong after a ping
    static const long timeout_pong = 5000;

    /// WebSocket Protocol version to use as a client
    /**
     * What version of the WebSocket Protocol to use for outgoing client
     * connections. Setting this to a value other than 13 (RFC6455) is not
     * recommended.
     */
    static const int client_version = 13; // RFC6455

    /// Default static error logging channels
    /**
     * Which error logging channels to enable at compile time. Channels not
     * enabled here will be unable to be selected by programs using the library.
     * This option gives an optimizing compiler the ability to remove entirely
     * code to test whether or not to print out log messages on a certain
     * channel
     *
     * Default is all except for development/debug level errors
     */
    static const websocketpp::log::level elog_level =
        websocketpp::log::elevel::all ^ websocketpp::log::elevel::devel;

    /// Default static access logging channels
    /**
     * Which access logging channels to enable at compile time. Channels not
     * enabled here will be unable to be selected by programs using the library.
     * This option gives an optimizing compiler the ability to remove entirely
     * code to test whether or not to print out log messages on a certain
     * channel
     *
     * Default is all except for development/debug level access messages
     */
    static const websocketpp::log::level alog_level =
        websocketpp::log::alevel::all;// ^ websocketpp::log::alevel::devel;

    ///
    static const size_t connection_read_buffer_size = 16384;

    /// Drop connections immediately on protocol error.
    /**
     * Drop connections on protocol error rather than sending a close frame.
     * Off by default. This may result in legit messages near the error being
     * dropped as well. It may free up resources otherwise spent dealing with
     * misbehaving clients.
     */
    static const bool drop_on_protocol_error = false;

    /// Suppresses the return of detailed connection close information
    /**
     * Silence close suppresses the return of detailed connection close
     * information during the closing handshake. This information is useful
     * for debugging and presenting useful errors to end users but may be
     * undesirable for security reasons in some production environments.
     * Close reasons could be used by an attacker to confirm that the endpoint
     * is out of resources or be used to identify the WebSocket implementation
     * in use.
     *
     * Note: this will suppress *all* close codes, including those explicitly
     * sent by local applications.
     */
    static const bool silent_close = false;

    /// Global flag for enabling/disabling extensions
    static const bool enable_extensions = true;

    /// Extension specific settings:

    /// permessage_deflate extension
    struct permessage_deflate_config {
        typedef core_client::request_type request_type;

        /// If the remote endpoint requests that we reset the compression
        /// context after each message should we honor the request?
        static const bool allow_disabling_context_takeover = true;

        static const bool server_no_context_takeover = false;
        static const bool client_no_context_takeover = false;
        
        static const uint8_t server_max_window_bits = 14;
        static const uint8_t client_max_window_bits = 14;

        /// If the remote endpoint requests that we reduce the size of the
        /// LZ77 sliding window size this is the lowest value that will be
        /// allowed. Values range from 8 to 15. A value of 8 means we will
        /// allow any possible window size. A value of 15 means do not allow
        /// negotiation of the window size (ie require the default).
        static const uint8_t client_min_window_bits = 8;
        static const uint8_t server_min_window_bits = 8;
    };

    typedef websocketpp::extensions::permessage_deflate::disabled
        <permessage_deflate_config> permessage_deflate_type;

    /// Autonegotiate permessage-compress
    /**
     * Automatically enables the permessage-compress extension.
     *
     * For clients this results in a permessage-compress extension request being
     * sent with every request rather than requiring it to be requested manually
     *
     * For servers this results in accepting the first set of extension settings
     * requested by the client that we understand being used. The alternative is
     * requiring the extension to be manually negotiated in `validate`. With
     * auto-negotiate on, you may still override the auto-negotiate manually if
     * needed.
     */
    static const bool autonegotiate_compression = true;

    struct mobile_signaling_config {
        typedef type::request_type request_type;
        typedef type::response_type response_type;
        typedef type::rng_type rng_type;
        static const bool primary_connection = true;
        static const bool override_coordinator = false;
    };
    typedef websocketpp::extensions::mobile_signaling::disabled
        <mobile_signaling_config> mobile_signaling_type;
};

} // namespace config
} // namespace websocketpp

#endif // WEBSOCKETPP_CONFIG_CORE_CLIENT_HPP
