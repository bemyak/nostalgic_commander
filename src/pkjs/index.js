var Clay = require('@rebble/clay');
var clayConfig = require('./config.js');
var clay = new Clay(clayConfig);

var weather = require('./weather.js');

// A failed fetch is otherwise not retried until the next :00/:30 tick, which
// leaves the watch blank for up to 30 minutes after a launch-time blip.
var WEATHER_MAX_RETRIES = 2;
var WEATHER_RETRY_DELAY_MS = 15 * 1000;

function retryWeather(attempt, reason) {
  if (attempt >= WEATHER_MAX_RETRIES) {
    console.log('Weather fetch failed (' + reason + '); retries exhausted');
    return;
  }
  console.log(
      'Weather fetch failed (' + reason + '); retrying in ' + (WEATHER_RETRY_DELAY_MS / 1000) +
      's');
  setTimeout(function() { getWeather(attempt + 1); }, WEATHER_RETRY_DELAY_MS);
}

function sendWeatherDict(dict, logLabel) {
  try {
    localStorage.setItem('weather-cache', JSON.stringify({payload: dict, fetchedAt: Date.now()}));
  } catch (e) {
    console.log('Error writing weather cache: ' + e);
  }
  Pebble.sendAppMessage(
      dict, function(e) { console.log(logLabel + ' sent successfully!'); },
      function(e) { console.log('Error sending: ' + JSON.stringify(e)); });
}

function readFreshWeatherCache() {
  try {
    var cache = JSON.parse(localStorage.getItem('weather-cache'));
    if (cache && cache.payload && weather.isFreshWeatherCache(cache.fetchedAt, Date.now())) {
      return cache.payload;
    }
  } catch (e) {
    console.log('Error reading weather cache: ' + e);
  }
  return null;
}

Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready!');
  // Resend a fresh cached payload so a watch with cleared storage still gets
  // data — localStorage plus an AppMessage, no radio.
  var cached = readFreshWeatherCache();
  if (cached) {
    console.log('Weather cache fresh, resending cached payload');
    Pebble.sendAppMessage(
        cached, function(e) { console.log('Cached weather sent successfully!'); },
        function(e) { console.log('Error sending: ' + JSON.stringify(e)); });
    // Cached by an older build: the resend is fine, but fill the missing
    // fields now instead of leaving new slots at "--" until the next edge.
    if (!weather.isCompleteWeatherPayload(cached)) {
      console.log('Cached payload predates current keys; fetching to complete');
      getWeather();
    }
  }
  // Nothing fresh cached: don't fetch proactively. The watch requests on
  // launch when its own cache is stale and a weather slot exists, retries a
  // dropped request on its side, and the appmessage listener always answers —
  // the watch holds the authoritative slot state, so mirroring it here would
  // only duplicate the request.
});

Pebble.addEventListener('appmessage', function(e) {
  // The watch asks on launch when its persisted cache is stale, and on a
  // fixed :00/:30 cadence regardless of cache age — always fetch.
  console.log('AppMessage received!');
  getWeather();
});

function getWeather(attempt) {
  attempt = attempt || 0;
  navigator.geolocation.getCurrentPosition(
      function(position) {
        var lat = position.coords.latitude;
        var lon = position.coords.longitude;

        var settings = {};
        try {
          settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
        } catch (e) {
          console.log('Error reading clay settings: ' + e);
        }
        // Units policy (default, string/int tolerance) lives in weather.js.
        var units = weather.unitsFromClaySettings(settings);

        // The modern `current` block carries temp/code/humidity in one shot;
        // `current_weather=true` is legacy and silently suppresses `current=`.
        var forecastUrl = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lon +
            '&current=temperature_2m,weather_code,relative_humidity_2m,precipitation,wind_direction_10m,wind_speed_10m' +
            '&timezone=auto' +
            '&temperature_unit=' + units.tempUnit + '&wind_speed_unit=' + units.windSpeedUnit +
            '&hourly=uv_index,precipitation_probability,temperature_2m&forecast_days=2' +
            '&daily=temperature_2m_max,temperature_2m_min';
        var aqiUrl = 'https://air-quality-api.open-meteo.com/v1/air-quality?latitude=' + lat +
            '&longitude=' + lon + '&current=us_aqi';

        // The forecast and AQI backends are independent; fan out and join so
        // the phone radio is up once instead of twice. The join preserves the
        // old serial semantics: a failed forecast retries the whole fetch
        // (an in-flight AQI result is discarded); a failed AQI sends with -1.
        var forecast = null;
        var aqi = -1;
        var settled = 0;
        var failedReason = null;

        function join() {
          settled++;
          if (settled < 2) return;
          if (failedReason) {
            retryWeather(attempt, failedReason);
            return;
          }
          // AQI rides in the forecast payload's dict; parse of the forecast
          // produced every other key.
          forecast.WEATHER_AQI = aqi;
          sendWeatherDict(forecast, 'Weather bundle');
        }

        var xhr = new XMLHttpRequest();
        xhr.onload = function() {
          if (xhr.status === 200) {
            try {
              forecast = weather.parseForecast(JSON.parse(this.responseText), Date.now());
            } catch (e) {
              failedReason = 'parse error: ' + e;
            }
          } else {
            failedReason = 'HTTP status ' + xhr.status;
          }
          join();
        };
        xhr.onerror = function() {
          failedReason = 'network error';
          join();
        };
        xhr.ontimeout = function() {
          failedReason = 'timeout';
          join();
        };
        xhr.open('GET', forecastUrl);
        xhr.timeout = 10000;
        xhr.send();

        var aqiXhr = new XMLHttpRequest();
        aqiXhr.onload = function() {
          if (aqiXhr.status === 200) {
            try {
              aqi = weather.parseAqi(JSON.parse(this.responseText));
            } catch (e) {
              console.log('Error parsing AQI: ' + e);
            }
          }
          join();
        };
        aqiXhr.onerror = join;
        aqiXhr.ontimeout = join;
        aqiXhr.open('GET', aqiUrl);
        aqiXhr.timeout = 10000;
        aqiXhr.send();
      },
      function(err) { retryWeather(attempt, 'geolocation: ' + err.message); },
      {timeout: 15000, maximumAge: weather.GEOLOCATION_MAX_AGE_MS});
}
