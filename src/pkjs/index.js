var Clay = require('@rebble/clay');
var clayConfig = require('./config.js');
var weather = require('./weather.js');

// Injected into the config page via toString (self-contained bodies only;
// `this` is the in-page ClayConfig): hide the CHIME_TEST flag input, and wire
// the test button to raise the flag and submit the form, so the ordinary
// webviewclosed path carries the flag to the watch.
function customFn(minified) {
  var clayConfig = this;
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    clayConfig.getItemByMessageKey('CHIME_TEST').hide();
    clayConfig.getItemById('test-sound').on('click', function() {
      clayConfig.getItemByMessageKey('CHIME_TEST').set('1');
      minified.$('#main-form').trigger('submit');
    });
  });
}

var clay = new Clay(clayConfig, customFn);

// The constructor registered Clay's webviewclosed handler first (handlers run
// in registration order), so by now the button press has flown to the watch
// as CHIME_TEST=1 and landed in localStorage. Stamp the flag back to 0, or
// the next plain "Save Settings" would re-beep.
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  clay.setSettings('CHIME_TEST', 0);
});

// A failed fetch is otherwise not retried until the next :00/:30 tick, which
// leaves the watch blank for up to 30 minutes after a launch-time blip.
var WEATHER_MAX_RETRIES = 2;
var WEATHER_RETRY_DELAY_MS = 15 * 1000;

// A reply lost to a momentary BT drop would otherwise keep the watch stale
// until its next :00/:30 tick; retry the send a couple of times.
var WEATHER_SEND_MAX_RETRIES = 2;
var WEATHER_SEND_RETRY_DELAY_MS = 5 * 1000;

var fetchGuard = weather.createFetchGuard();
var weatherRetryTimer = null;

// Starts the rerun a queued trigger is owed, if any.
function endWeatherFetch() {
  if (!fetchGuard.end()) return false;
  getWeather(0);
  return true;
}

function retryWeather(attempt, reason) {
  if (attempt >= WEATHER_MAX_RETRIES) {
    console.log('Weather fetch failed (' + reason + '); retries exhausted');
    endWeatherFetch();
    return;
  }
  if (!fetchGuard.retry()) {
    getWeather(0);
    return;
  }
  console.log(
      'Weather fetch failed (' + reason + '); retrying in ' + (WEATHER_RETRY_DELAY_MS / 1000) +
      's');
  weatherRetryTimer = setTimeout(function() {
    fetchGuard.resume();
    getWeather(attempt + 1);
  }, WEATHER_RETRY_DELAY_MS);
}

function doSendWeatherDict(dict, logLabel, attempt) {
  Pebble.sendAppMessage(
      dict,
      function() {
        console.log(logLabel + ' sent successfully!');
        endWeatherFetch();
      },
      function(e) {
        if (attempt < WEATHER_SEND_MAX_RETRIES) {
          var next = attempt + 1;
          setTimeout(
              function() { doSendWeatherDict(dict, logLabel, next); }, WEATHER_SEND_RETRY_DELAY_MS);
          return;
        }
        console.log('Error sending ' + logLabel + ': ' + JSON.stringify(e));
        endWeatherFetch();
      });
}

function readClaySettings() {
  try {
    return JSON.parse(localStorage.getItem('clay-settings')) || {};
  } catch (e) {
    console.log('Error reading clay settings: ' + e);
    return {};
  }
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready!');
  try {
    localStorage.removeItem('weather-cache');
  } catch (e) {
    console.log('Error clearing weather cache: ' + e);
  }
  // Don't fetch proactively. The watch requests on launch when its own cache
  // is stale and a weather slot exists, retries a dropped request on its side,
  // and the appmessage listener always answers — the watch holds the
  // authoritative slot state, so mirroring it here would only duplicate the
  // request.
});

Pebble.addEventListener('appmessage', function() {
  // The watch asks on launch when its persisted cache is stale, and on a
  // fixed :00/:30 cadence regardless of cache age — always fetch.
  console.log('AppMessage received!');
  clearTimeout(weatherRetryTimer);
  if (!fetchGuard.begin()) {
    console.log('Weather fetch already in flight; queueing one more');
    return;
  }
  getWeather(0);
});

// `callback(error, json)` runs exactly once per request, whichever of
// load/error/timeout fires first.
function fetchJson(url, callback) {
  var settled = false;
  function settle(error, json) {
    if (settled) return;
    settled = true;
    callback(error, json);
  }
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status !== 200) {
      settle('HTTP status ' + xhr.status);
      return;
    }
    var json;
    try {
      json = JSON.parse(xhr.responseText);
    } catch (e) {
      settle('parse error: ' + e);
      return;
    }
    settle(null, json);
  };
  xhr.onerror = function() { settle('network error'); };
  xhr.ontimeout = function() { settle('timeout'); };
  xhr.open('GET', url);
  xhr.timeout = 10000;
  xhr.send();
}

function getWeather(attempt) {
  navigator.geolocation.getCurrentPosition(
      function(position) {
        var settings = readClaySettings();
        var units = weather.unitsFromClaySettings(settings);
        var windowHours = weather.windowHoursFromClaySettings(settings);
        var wantAqi = weather.needsAqi(settings);
        var lat = position.coords.latitude;
        var lon = position.coords.longitude;

        // The forecast and AQI backends are independent; fan out and join so
        // the phone radio is up once instead of twice. A failed forecast
        // retries the whole fetch (an in-flight AQI result is discarded); a
        // failed AQI sends with -1. With no AQI slot the AQI request is
        // skipped and the field keeps the -1 the forecast parse left in it.
        var forecast = null;
        var aqi = -1;
        var expected = wantAqi ? 2 : 1;
        var settled = 0;
        var failedReason = null;

        function join() {
          settled++;
          if (settled < expected) return;
          if (failedReason) {
            retryWeather(attempt, failedReason);
            return;
          }
          if (!fetchGuard.shouldSendResult()) {
            console.log('Weather result predates a queued trigger; discarding');
            endWeatherFetch();
            return;
          }
          if (wantAqi) forecast.WEATHER_AQI = aqi;
          forecast.LONGITUDE = weather.longitudeCenti(lon);
          doSendWeatherDict(forecast, 'Weather bundle', 0);
        }

        fetchJson(weather.buildForecastUrl(lat, lon, units), function(error, json) {
          if (error) {
            failedReason = error;
          } else {
            try {
              forecast = weather.parseForecast(json, Date.now(), windowHours);
            } catch (e) {
              failedReason = 'parse error: ' + e;
            }
          }
          join();
        });

        if (wantAqi) {
          fetchJson(weather.buildAqiUrl(lat, lon), function(error, json) {
            if (!error) {
              try {
                aqi = weather.parseAqi(json);
              } catch (e) {
                console.log('Error parsing AQI: ' + e);
              }
            }
            join();
          });
        }
      },
      function(err) {
        // PERMISSION_DENIED (code 1) won't change under retrying (it would
        // only re-prompt); leave "--" on the watch.
        if (err.code === 1) {
          console.log('Geolocation permission denied; no retry');
          endWeatherFetch();
          return;
        }
        retryWeather(attempt, 'geolocation: ' + err.message);
      },
      {timeout: 15000, maximumAge: weather.GEOLOCATION_MAX_AGE_MS});
}
