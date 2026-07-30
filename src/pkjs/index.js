var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

var WEATHER_CACHE_MAX_AGE_MS = 30 * 60 * 1000;

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

// The UV complication shows the peak over the coming window, not a calendar
// day max (which is mostly about the past by evening) and not the instant
// value (which reads 0 whenever the sun is low).
var UV_WINDOW_HOURS = 12;

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
    if (cache && cache.payload && (Date.now() - cache.fetchedAt) < WEATHER_CACHE_MAX_AGE_MS) {
      return cache.payload;
    }
  } catch (e) {
    console.log('Error reading weather cache: ' + e);
  }
  return null;
}

Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready!');
  // The watchface relaunches every time the user navigates back to it;
  // don't hit the network if the last fetch is still fresh. Resend the
  // cached payload so a watch with cleared storage still gets data.
  var cached = readFreshWeatherCache();
  if (cached) {
    console.log('Weather cache fresh, resending cached payload');
    Pebble.sendAppMessage(
        cached, function(e) { console.log('Cached weather sent successfully!'); },
        function(e) { console.log('Error sending: ' + JSON.stringify(e)); });
  } else {
    getWeather();
  }
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

        // Read units from Clay settings
        var settings = {};
        try {
          settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
        } catch (e) {
          console.log('Error reading clay settings: ' + e);
        }
        var units = settings['SETTINGS_UNITS'] || '0';
        var tempUnit = (units === '1' || units === 1) ? 'celsius' : 'fahrenheit';

        var forecastUrl = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lon +
            '&current_weather=true&timezone=auto&temperature_unit=' + tempUnit +
            '&hourly=uv_index&forecast_hours=' + UV_WINDOW_HOURS;
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
          sendWeatherDict(
              {
                'WEATHER_TEMP': forecast.temp,
                'WEATHER_COND': forecast.cond,
                'WEATHER_AQI': aqi,
                'WEATHER_UV': forecast.uv
              },
              'Weather, AQI & UV');
        }

        var xhr = new XMLHttpRequest();
        xhr.onload = function() {
          if (xhr.status === 200) {
            try {
              var json = JSON.parse(this.responseText);
              var temp = Math.round(json.current_weather.temperature);
              var code = json.current_weather.weathercode;
              // -1 is the watch-side "no data" sentinel; forecast_hours
              // already windows the hourly data, the timestamp guard keeps us
              // honest if the API ever returns a wider range.
              var uv = -1;
              if (json.hourly && json.hourly.uv_index && json.hourly.time) {
                var windowStart = Date.now() - 3600 * 1000;  // include the in-progress hour
                var windowEnd = Date.now() + UV_WINDOW_HOURS * 3600 * 1000;
                for (var i = 0; i < json.hourly.uv_index.length; i++) {
                  var v = json.hourly.uv_index[i];
                  var t = new Date(json.hourly.time[i]).getTime();
                  if (typeof v === 'number' && t >= windowStart && t <= windowEnd && v > uv) {
                    uv = v;
                  }
                }
                if (uv >= 0) uv = Math.round(uv);
              }

              var cond = 'SUN';
              if (code === 0) {
                cond = 'SUN';
              } else if (code >= 1 && code <= 3) {
                cond = 'CLD';
              } else if (code === 45 || code === 48) {
                cond = 'FOG';
              } else if (
                  (code >= 51 && code <= 55) || (code >= 61 && code <= 65) ||
                  (code >= 80 && code <= 82)) {
                cond = 'RAIN';
              } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
                cond = 'SNOW';
              } else if (code >= 95) {
                cond = 'TSTM';
              } else {
                cond = 'CLD';
              }
              forecast = {temp: temp, cond: cond, uv: uv};
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
              var aqiJson = JSON.parse(this.responseText);
              if (aqiJson.current && aqiJson.current.us_aqi !== undefined) {
                aqi = Math.round(aqiJson.current.us_aqi);
              }
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
      {timeout: 15000, maximumAge: WEATHER_CACHE_MAX_AGE_MS});
}
