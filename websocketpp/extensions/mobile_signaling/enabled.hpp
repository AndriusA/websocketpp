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

#ifndef WEBSOCKETPP_EXTENSION_MOBILE_SIGNALING_ENABLED_HPP
#define WEBSOCKETPP_EXTENSION_MOBILE_SIGNALING_ENABLED_HPP

#include <websocketpp/common/cpp11.hpp>
#include <websocketpp/common/memory.hpp>
#include <websocketpp/common/system_error.hpp>
#include <websocketpp/error.hpp>
#include <websocketpp/utilities.hpp>
#include <websocketpp/extensions/extension.hpp>
#include <websocketpp/uri.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace websocketpp {
namespace extensions {
namespace mobile_signaling {    
/// Permessage deflate error values
namespace error {
enum value {
    /// Catch all
    general = 1,

    /// Invalid extension attributes
    invalid_attributes,

    /// Invalid extension attribute value
    invalid_attribute_value,

    /// Invalid megotiation mode
    invalid_mode,

    /// Unsupported extension attributes
    unsupported_attributes,

    /// Uninitialized
    uninitialized,

    /// Configuration error
    configuration_error
};

/// Mobile-signaling error category
class category : public lib::error_category {
public:
    category() {}

    char const * name() const _WEBSOCKETPP_NOEXCEPT_TOKEN_ {
        return "websocketpp.extension.mobile-signaling";
    }

    std::string message(int value) const {
        switch(value) {
            case general:
                return "Generic mobile-signaling error";
            case invalid_attributes:
                return "Invalid extension attributes";
            case invalid_attribute_value:
                return "Invalid extension attribute value";
            case invalid_mode:
                return "Invalid pmobile-signaling negotiation mode";
            case unsupported_attributes:
                return "Unsupported extension attributes";
            case uninitialized:
                return "Mobile signaling extension must be initialized before use";
            case configuration_error:
                return "Error in mobile-signaling extension configuration";
            default:
                return "Unknown mobile-signaling error";
        }
    }
};

/// Get a reference to a static copy of the mobile-signaling error category
lib::error_category const & get_category() {
    static category instance;
    return instance;
}

/// Create an error code in the mobile-signaling category
lib::error_code make_error_code(error::value e) {
    return lib::error_code(static_cast<int>(e), get_category());
}

} // namespace error
} // namespace mobile_signaling
} // namespace extensions
} // namespace websocketpp

_WEBSOCKETPP_ERROR_CODE_ENUM_NS_START_
template<> struct is_error_code_enum
    <websocketpp::extensions::mobile_signaling::error::value>
{
    static bool const value = true;
};
_WEBSOCKETPP_ERROR_CODE_ENUM_NS_END_


namespace websocketpp {
namespace extensions {
namespace mobile_signaling {

template <typename config>
class enabled {
public:

    typedef typename config::request_type request_type;
    typedef typename config::response_type response_type;

    enabled()
      : m_enabled(false)
      , m_primary_connection(config::primary_connection)
      , m_coordinator("")
      , m_destination("")
      , m_initialized(false)
    {
        m_coordinator = config::coordinator();
        //constructor
    }

    ~enabled() {
        if (!m_initialized) {
            return;
        }

        //destructor
    }

    /// Initialize state for the extension
    /**
     * Note: this should be called *after* the negotiation methods. It will use
     * information from the negotiation to determine how to initialize the data structures
     *
     * @param is_server Whether or not to initialize as a server or client.
     * @return A code representing the error that occurred, if any
     */
    lib::error_code init() {
        m_initialized = true;
        return lib::error_code();
    }

    /// Test if this object impliments the extension specification
    /**
     * Because this object does impliment it, it will always return true.
     *
     * @return Whether or not this object impliments mobile-signaling
     */
    bool is_implemented() const {
        return true;
    }

    /// Test if the extension was negotiated for this connection
    /**
     * Retrieves whether or not this extension is in use based on the initial
     * handshake extension negotiations.
     *
     * @return Whether or not the extension is in use
     */
    bool is_enabled() const {
        return m_enabled;
    }

    /// Generate extension offer
    /**
     * Creates an offer string to include in the Sec-WebSocket-Extensions
     * header of outgoing client requests.
     *
     * @todo harcoded values for now...
     * 
     * @return A WebSocket extension offer string for this extension
     */
    err_str_pair generate_offer() const {
        std::cout << "generate offer " << std::endl;
        err_str_pair ret;
        ret.second = "mobile-signaling";

        // TODO: how do we generate connection_id?
        ret.second += "; connection_id=alvbjajodbaodvb";

        // The type of connection is based on config
        // The direct connection will always be the primary one
        // and the secondary one will always and only be from a proxy
        // hence configured at compile time
        if (config::primary_connection)
            ret.second += "; primary";
        ret.second += "; coordinator=\"" + config::coordinator().str() + "\"";
        // if (!config::primary_connection)
            ret.second += "; destination=\"" + config::destination().str() + "\"";
        return ret;
    }

    /// Validate extension response
    /**
     * Confirm that the server has negotiated settings compatible with our
     * original offer and apply those settings to the extension state.
     *
     * @param response The server response attribute list to validate
     * @return Validation error or 0 on success
     */
    lib::error_code validate_response(http::attribute_list const & response) {
        lib::error_code err;
        http::attribute_list::const_iterator it;
        bool id, primary, coordinator, destination;
        for (it = response.begin(); it != response.end(); ++it) {
            if (it->first == "connection_id") {
                if (it->second.empty() || id)
                    // If the value is not set or we have already seen the attribute in attributes
                    err = make_error_code(error::invalid_attributes);
                else if (false)
                    // Also check if the ID has the correct value...
                    err = make_error_code(error::invalid_attribute_value);
                else
                    id = true;
            } else if (it->first == "primary" || it->first == "secondary") {
                if (!it->second.empty() || primary)
                    err = make_error_code(error::invalid_attributes);
                else
                    primary = true;
            } else if (it->first == "coordinator") {
                if (it->second.empty() || coordinator)
                    // We need to have a coordinator and there can only be one
                    err = make_error_code(error::invalid_attributes);
                else
                    // TODO: verify the URL maybe?
                    coordinator = true;
            } else if (it->first == "destination") {
                if (it->second.empty() || destination)
                    err = make_error_code(error::invalid_attributes);
                else
                    //TODO: verify url?
                    destination = true;
            } else {
                // No other attributes are allowed
                err = make_error_code(error::invalid_attributes);
            }
            if (err)
                break;
        }
        if (!id || !coordinator)// || (primary && !destination))
            // All attributes MUST be present
            err = make_error_code(error::invalid_attributes);

        return err;
    }

    lib::error_code process_response(http::attribute_list const & response) {
        lib::error_code err;
        if (err == lib::error_code())
            m_enabled = true;
        return err;
    }

    /// Negotiate extension
    /**
     * Confirm that the client's extension negotiation offer has settings
     * compatible with local policy. If so, generate a reply and apply those
     * settings to the extension state.
     *
     * @param offer Attribute from client's offer
     * @return Status code and value to return to remote endpoint
     */
    err_str_pair negotiate_request(http::attribute_list const & offer) {
        err_str_pair ret;

        // TODO: negotiate parameters to respond to the client
        http::attribute_list::const_iterator it;
        for (it = offer.begin(); it != offer.end(); ++it) {
            std::cout << "negotiating " << it->first << " - " << it->second << std::endl;
            // TODO: verify negotiation logic based on the spec...
            if (it->first == "connection_id") {
                generate_server_connection_id(it->second,ret.first);
            } else if (it->first == "primary") {
                // TODO: would verify the protocol logic of what gets established when and how..
                m_primary_connection = true;
            } else if (it->first == "coordinator") {
                negotiate_coordinator(it->second,ret.first);
            } else if (it->first == "destination") {
                //skip - destination should be ok
                websocketpp::uri dest(it->second);
                m_destination = dest;
                std::cout << "request destination " << it->second << std::endl;
            } else {
                ret.first = make_error_code(error::invalid_attributes);
            }

            if (ret.first) {
                break;
            }
        }

        if (ret.first == lib::error_code()) {
            m_enabled = true;
            ret.second = generate_response();
            std::cout << "generated response" << ret.second << std::endl;
        }

        return ret;
    }

private:
    /// Generate negotiation response
    /**
     * @return Generate extension negotiation reponse string to send to client
     */
    std::string generate_response() {
        std::string ret;
        ret = "mobile-signaling";
        ret += "; connection_id=" + m_connection_id;
        if (m_primary_connection)
            ret += "; primary";
        ret += "; coordinator=\""+m_coordinator.str() + "\"";
        if (m_destination.get_valid())
            ret += "; destination=\""+m_destination.str() + "\"";
        return ret;
    }

    /// Negotiate coordinator attribute
    /**
     * Negotiate coordinator URL between the offered and configured values.
     * Depending on the config can either use the offered one but only set one
     * if none is offered, or force a specific proxy
     *
     * @param [in] value The offered URL of the proxy
     * @param [out] ec A reference to the error code to return errors via
     */
    void negotiate_coordinator(std::string const & value, lib::error_code & ec) {
        websocketpp::uri offer(value);
        if (!offer.get_valid()) {
            if (config::coordinator().get_valid())
                // use own pre-configured coordinator if none offered
                m_coordinator = config::coordinator();
            else
                // no coordinator to use, must have one
                ec = make_error_code(error::invalid_attribute_value);
        } else {
            if (!config::override_coordinator) {
                // Offered coordinator URI is valid and we don't want to override
                m_coordinator = offer;
            }
            else if (config::coordinator().get_valid())
                m_coordinator = config::coordinator();
            else
                // We want to override, but configure URI is invalid
                ec = make_error_code(error::configuration_error);
        }        
    }

    void generate_server_connection_id(std::string const & value, lib::error_code & ec) {
        // TODO: what's the algorithm gonna be?
        m_connection_id = value;
    }

    std::string m_connection_id;
    bool m_enabled;
    bool m_primary_connection;
    websocketpp::uri m_coordinator;
    websocketpp::uri m_destination;
    bool m_initialized;
};

} // namespace mobile_signaling
} // namespace extensions
} // namespace websocketpp

#endif // WEBSOCKETPP_EXTENSION_MOBILE_SIGNALING_ENABLED_HPP
