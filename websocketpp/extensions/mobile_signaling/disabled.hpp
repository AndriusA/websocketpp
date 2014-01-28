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

#ifndef WEBSOCKETPP_EXTENSION_MOBILE_SIGNALING_DISABLED_HPP
#define WEBSOCKETPP_EXTENSION_MOBILE_SIGNALING_DISABLED_HPP

#include <websocketpp/common/cpp11.hpp>
#include <websocketpp/common/system_error.hpp>
#include <websocketpp/error.hpp>

#include <websocketpp/http/constants.hpp>
#include <websocketpp/extensions/extension.hpp>

#include <map>
#include <string>
#include <utility>

namespace websocketpp {
namespace extensions {
namespace mobile_signaling {

/// Stub class for use when disabling mobile_signaling extension
/**
 * This class is a stub that implements the mobile_signaling interface
 * with minimal dependencies. It is used to disable mobile_signaling
 * functionality at compile time without loading any unnecessary code.
 */
template <typename config>
class disabled {
    typedef std::pair<lib::error_code,std::string> err_str_pair;
    typedef typename config::request_type request_type;

public:
    err_str_pair negotiate_request(http::attribute_list const & attributes) {
        return make_pair(make_error_code(error::disabled),std::string());
    }
    lib::error_code validate_response(http::attribute_list const & response) {
        return make_error_code(error::disabled);
    }

    lib::error_code process_response(http::attribute_list const & response) {
        return make_error_code(error::disabled);
    }        

    /// Initialize state
    lib::error_code init() {
        return lib::error_code();
    }

    /// Returns true if the extension is capable of providing
    /// mobile_signaling functionality
    bool is_implemented() const {
        return false;
    }

    /// Returns true if mobile_signaling functionality is active for this
    /// connection
    bool is_enabled() const {
        return false;
    }

    err_str_pair generate_offer(uri_ptr uri) const {
        err_str_pair ret;
        ret.first = make_error_code(error::disabled);
        return ret;
    }
};

} // namespace mobile_signaling
} // namespace extensions
} // namespace websocketpp

#endif // WEBSOCKETPP_EXTENSION_MOBILE_SIGNALING_DISABLED_HPP
