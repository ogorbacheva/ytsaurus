var events = require("events");
var os = require("os");
var util = require("util");

var Q = require("q");

var YtAccrualFailureDetector = require("./accrual_failure_detector").that;
var YtError = require("./error").that;
var utils = require("./utils");

////////////////////////////////////////////////////////////////////////////////

var __DBG = require("./debug").that("X", "Coordinator");

////////////////////////////////////////////////////////////////////////////////

function YtCoordinatedHost(config, host)
{
    "use strict";

    var role = "control";
    var dead = true;
    var banned = false;
    var liveness = { updated_at: new Date(0), load_average: 0.0 };
    var randomness = Math.random();

    var afd = new YtAccrualFailureDetector(
        config.afd_window_size,
        config.afd_phi_threshold,
        config.heartbeat_drift,
        config.heartbeat_interval + config.heartbeat_drift,
        config.heartbeat_interval);

    var self = this;

    Object.defineProperty(this, "host", {
        value: host,
        writable: false,
        enumerable: true
    });

    Object.defineProperty(this, "role", {
        get: function() { return role; },
        set: function(value) {
            if (value !== "control" && value !== "data") {
                throw new TypeError("Role has to be either 'control' or 'data'");
            }

            role = value;
        },
        enumerable: true
    });

    Object.defineProperty(this, "dead", {
        get: function() { return dead; },
        set: function(value) {
            var dead_before = dead;
            dead = typeof(value) === "string" ? value === "true" : !!value;

            if (!dead_before && dead) {
                self.emit("dead");
            } else if (dead_before && !dead) {
                self.emit("alive");
            }
        },
        enumerable: true
    });

    Object.defineProperty(this, "banned", {
        get: function() { return banned; },
        set: function(value) {
            var banned_before = banned;
            banned = typeof(value) === "string" ? value === "true" : !!value;

            if (!banned_before && banned) {
                self.emit("banned");
            } else if (banned_before && !banned) {
                self.emit("unbanned");
            }
        },
        enumerable: true
    });

    Object.defineProperty(this, "liveness", {
        get: function() { return liveness; },
        set: function(value) {
            if (typeof(liveness) !== "object") {
                throw new TypeError("Liveness has to be an object");
            }

            liveness.updated_at = new Date(value.updated_at);
            liveness.load_average = parseFloat(value.load_average);
            randomness = Math.random();
            afd.heartbeatTS(liveness.updated_at);
        },
        enumerable: true
    });

    Object.defineProperty(this, "randomness", {
        value: randomness,
        writable: false,
        enumerable: true
    });

    Object.defineProperty(this, "afd_sample", {
        get: function() {
            return {
                length: afd.sample.length,
                mean: afd.sample.mean,
                stddev: afd.sample.stddev
            };
        },
        enumerable: true
    });

    Object.defineProperty(this, "afd_phi", {
        get: function() {
            return afd.phiTS();
        },
        enumerable: true
    });

    Object.defineProperty(this, "fitness", {
        get: function() {
            return 0.0 +
                config.fitness_la_coefficient * liveness.load_average +
                config.fitness_phi_coefficient * afd.phiTS() +
                config.fitness_randomness * randomness;
        },
        enumerable: true
    });

    events.EventEmitter.call(this);

    // Hide EventEmitter properties to clean up JSON.
    Object.defineProperty(this, "_events", { enumerable: false });
    Object.defineProperty(this, "_maxListeners", { enumerable: false });
    Object.defineProperty(this, "domain", { enumerable: false });
}

util.inherits(YtCoordinatedHost, events.EventEmitter);

function YtCoordinator(config, logger, driver, fqdn)
{
    "use strict";
    this.__DBG = __DBG.Tagged();

    this.config = config;
    this.logger = logger;
    this.driver = driver;

    this.fqdn = fqdn;
    this.host = new YtCoordinatedHost(this.config, this.fqdn);
    this.hosts = {};
    this.hosts[this.fqdn] = this.host;

    if (this.config.enable) {
        this.initialized = false;
        this.timer = setInterval(this._refresh.bind(this), this.config.heartbeat_interval);
        this.timer.unref && this.timer.unref();
    }

    this.__DBG("New");
}

YtCoordinator.prototype._refresh = function()
{
    "use strict";

    var self = this;
    var fqdn = self.fqdn;
    var path = "//sys/proxies/" + utils.escapeYPath(fqdn);

    if (!self.initialized) {
        return Q
        .when()
        .then(function() {
            return self.driver.executeSimple("exists", { path: path });
        })
        .then(function(exists) {
            if (exists === "true") {
                return;
            }
            return self.driver.executeSimple("create", {
                type: "map_node",
                path: path
            });
        })
        .then(function(create) {
            var req1 = self.driver.executeSimple(
                "set",
                { path: path + "/@role" },
                "data");
            var req2 = self.driver.executeSimple(
                "set",
                { path: path + "/@banned" },
                "false");
            return Q.all([ req1, req2 ]);
        })
        .then(function() {
            self.initialized = true;
            return self._refresh();
        })
        .fail(function(err) {
            var error = YtError.ensureWrapped(err);
            self.logger.error(
                "An error occured while initializing coordination",
                // TODO(sandello): Embed.
                { error: error.toJson() });
        });
    }

    self.__DBG("Updating coordination information");

    return Q
    .when()
    .then(function() {
        return self.driver.executeSimple("set", { path: path + "/@liveness" }, {
            updated_at: (new Date()).toISOString(),
            load_average: os.loadavg()[2]
        });
    })
    .then(function() {
        return self.driver.executeSimple("list", {
            path: "//sys/proxies",
            attributes: [ "role", "banned", "liveness" ]
        });
    })
    .then(function(entries) {
        entries.forEach(function(entry) {
            var host = utils.getYsonValue(entry);

            var ref = self.hosts[host];
            if (typeof(ref) === "undefined") {
                self.logger.info("Discovered a new proxy", { host: host });
                ref = self.hosts[host] = new YtCoordinatedHost(self.config, host);

                ref.on("dead", function() {
                    self.logger.info("Marking proxy as dead", { host: host });
                });
                ref.on("alive", function() {
                    self.logger.info("Marking proxy as alive", { host: host });
                });
                ref.on("banned", function() {
                    self.logger.info("Proxy was banned", { host: banned });
                });
                ref.on("unbanned", function() {
                    self.logger.info("Proxy was unbanned", { host: unbanned });
                });
            }

            self.__DBG("Proxy '%s' has been updated to %j", host, entry);

            ref.role = utils.getYsonAttribute(entry, "role");
            ref.banned = utils.getYsonAttribute(entry, "banned");
            ref.liveness = utils.getYsonAttribute(entry, "liveness");
            ref.dead = (new Date() - ref.liveness.updated_at) > self.config.death_age;
        });
    })
    .fail(function(err) {
        var error = YtError.ensureWrapped(err);
        self.logger.error(
            "An error occured while initializing coordination",
            // TODO(sandello): Embed.
            { error: error.toJson() });
    });
};

YtCoordinator.prototype.getControlProxy = function()
{
    "use strict";
    return this
    .getProxies("control", false, false)
    .sort(function(lhs, rhs) { return lhs.fitness - rhs.fitness; })
    [0];
};

YtCoordinator.prototype.getDataProxy = function()
{
    "use strict";
    return this
    .getProxies("data", false, false)
    .sort(function(lhs, rhs) { return lhs.fitness - rhs.fitness; })
    [0];
};

YtCoordinator.prototype.getProxies = function(role, dead, banned)
{
    "use strict";
    var result = [];
    for (var p in this.hosts) {
        if (this.hosts.hasOwnProperty(p)) {
            var ref = this.hosts[p];

            if (typeof(role) !== "undefined" && role !== ref.role) {
                continue;
            }
            if (typeof(dead) !== "undefined" && dead !== ref.dead) {
                continue;
            }
            if (typeof(dead) !== "undefined" && banned !== ref.banned) {
                continue;
            }
            result.push(this.hosts[p]);
        }
    }
    return result;
};

YtCoordinator.prototype.getSelf = function()
{
    return this.host;
};

////////////////////////////////////////////////////////////////////////////////

exports.that = YtCoordinator;
