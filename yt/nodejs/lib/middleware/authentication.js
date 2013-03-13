var YtAuthentication = require("../authentication").that;
var YtRegistry = require("../registry").that;

////////////////////////////////////////////////////////////////////////////////

exports.that = function Middleware__YtAuthentication()
{
    "use strict";

    var config = YtRegistry.get("config", "authentication");
    var logger = YtRegistry.get("logger");
    var authority = YtRegistry.get("authority");

    return function(req, rsp, next) {
        return (new YtAuthentication(
            config,
            req.logger || logger,
            authority))
        .dispatch(req, rsp, next);
    };
};
