var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

var WEATHER_CACHE_MAX_AGE_MS = 30 * 60 * 1000;

// Everything the watch consumes. The whole reply is cached verbatim and
// resent on ready; an older build's cache is missing newer keys — safe (the
// watch guards each field) but worth completing at once rather than at the
// next :00/:30 edge.
var WEATHER_DICT_KEYS = [
  'WEATHER_TEMP', 'WEATHER_COND', 'WEATHER_AQI', 'WEATHER_UV', 'WEATHER_HUMIDITY', 'WEATHER_PCP',
  'WEATHER_HIGH', 'WEATHER_LOW', 'WEATHER_PRECIP_NOW', 'WEATHER_LOW_TOMORROW',
  'WEATHER_TEMP_HIGH_TOMORROW', 'WEATHER_HI_HOUR_TODAY', 'WEATHER_LO_HOUR_TODAY',
  'WEATHER_HI_HOUR_TOMORROW', 'WEATHER_LO_HOUR_TOMORROW', 'WEATHER_WIND_DIRECTION',
  'WEATHER_WIND_SPEED'
];

function isCompleteWeatherPayload(payload) {
  return WEATHER_DICT_KEYS.every(function(k) { return payload[k] !== undefined; });
}

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
    if (!isCompleteWeatherPayload(cached)) {
      console.log('Cached payload predates current keys; fetching to complete');
      getWeather();
    }
  }
  // Nothing fresh cached: don't fetch proactively. The watch requests on
  // launch when its own cache is stale and a weather slot exists, retries a
  // dropped request (Step 4), and the appmessage listener always answers — the
  // watch holds the authoritative slot state, so mirroring it here would only
  // duplicate the request.
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
        var windSpeedUnit = (units === '1' || units === 1) ? 'ms' : 'mph';

        // The modern `current` block carries temp/code/humidity in one shot;
        // `current_weather=true` is legacy and silently suppresses `current=`.
        var forecastUrl = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lon +
            '&current=temperature_2m,weather_code,relative_humidity_2m,precipitation,wind_direction_10m,wind_speed_10m' +
            '&timezone=auto' +
            // The unit param belongs to Open-Meteo's own vocabulary: gusts
            // answer the same wind_speed_unit= knob as speed does.
            '&temperature_unit=' + tempUnit + '&wind_speed_unit=' + windSpeedUnit +
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
          sendWeatherDict(
              {
                'WEATHER_TEMP': forecast.temp,
                'WEATHER_COND': forecast.cond,
                'WEATHER_AQI': aqi,
                'WEATHER_UV': forecast.uv,
                'WEATHER_HUMIDITY': forecast.humidity,
                'WEATHER_WIND_DIRECTION': forecast.windDirection,
                'WEATHER_WIND_SPEED': forecast.windSpeed,
                'WEATHER_PCP': forecast.pcp,
                'WEATHER_PRECIP_NOW': forecast.precipNow,
                'WEATHER_HIGH': forecast.high,
                'WEATHER_LOW': forecast.low,
                'WEATHER_LOW_TOMORROW': forecast.lowTmrw,
                'WEATHER_TEMP_HIGH_TOMORROW': forecast.highTmrw,
                'WEATHER_HI_HOUR_TODAY': forecast.hiHourToday,
                'WEATHER_LO_HOUR_TODAY': forecast.loHourToday,
                'WEATHER_HI_HOUR_TOMORROW': forecast.hiHourTmrw,
                'WEATHER_LO_HOUR_TOMORROW': forecast.loHourTmrw
              },
              'Weather bundle');
        }

        var xhr = new XMLHttpRequest();
        xhr.onload = function() {
          if (xhr.status === 200) {
            try {
              var json = JSON.parse(this.responseText);
              var temp = Math.round(json.current.temperature_2m);
              var code = json.current.weather_code;
              // -1 is the watch-side "no data" sentinel; a missing field means
              // the API shape changed, not that the air is perfectly dry.
              var humidity = -1;
              if (typeof json.current.relative_humidity_2m === 'number') {
                humidity = Math.round(json.current.relative_humidity_2m);
              }
              // Meteo FROM bearing, whole degrees; the watch flips it to the
              // direction the wind blows. Same missing-field guard as humidity.
              var windDirection = -1;
              if (typeof json.current.wind_direction_10m === 'number') {
                windDirection = Math.round(json.current.wind_direction_10m);
              }
              var windSpeed = -1;
              if (typeof json.current.wind_speed_10m === 'number') {
                windSpeed = Math.round(json.current.wind_speed_10m);
              }
              // Tenths of mm over the past hour. Always mm — the watch
              // displays the live rate only in metric mode, so no unit param.
              var precipNow = -1;
              if (typeof json.current.precipitation === 'number') {
                precipNow = Math.round(json.current.precipitation * 10);
              }
              // -1 is the watch-side "no data" sentinel; the timestamp guard
              // windows what the API returns (the series now spans two days).
              // Both readings below are next-12h maxima, computed in one pass.
              var uv = -1;
              var pcp = -1;
              if (json.hourly && json.hourly.time) {
                var windowStart = Date.now() - 3600 * 1000;  // include the in-progress hour
                var windowEnd = Date.now() + UV_WINDOW_HOURS * 3600 * 1000;
                var uvArr = json.hourly.uv_index || [];
                var pcpArr = json.hourly.precipitation_probability || [];
                for (var i = 0; i < json.hourly.time.length; i++) {
                  var t = new Date(json.hourly.time[i]).getTime();
                  if (!(t >= windowStart && t <= windowEnd)) continue;  // NaN-safe
                  if (typeof uvArr[i] === 'number' && uvArr[i] > uv) uv = uvArr[i];
                  // The API nulls probability where no precip is forecast at
                  // all; a window of nulls at least still reads "no data".
                  if (typeof pcpArr[i] === 'number' && pcpArr[i] > pcp) pcp = pcpArr[i];
                }
              }
              if (uv >= 0) uv = Math.round(uv);
              if (pcp >= 0) pcp = Math.round(pcp);

              var daily = json.daily || {};
              var high = -999;
              var low = -999;
              if (daily.temperature_2m_max && typeof daily.temperature_2m_max[0] === 'number') {
                high = Math.round(daily.temperature_2m_max[0]);
              }
              if (daily.temperature_2m_min && typeof daily.temperature_2m_min[0] === 'number') {
                low = Math.round(daily.temperature_2m_min[0]);
              }
              // Tomorrow's pair — each HI/LO cell rolls to these an hour
              // after its own extreme passes.
              var lowTmrw = -999;
              if (daily.temperature_2m_min && typeof daily.temperature_2m_min[1] === 'number') {
                lowTmrw = Math.round(daily.temperature_2m_min[1]);
              }
              var highTmrw = -999;
              if (daily.temperature_2m_max && typeof daily.temperature_2m_max[1] === 'number') {
                highTmrw = Math.round(daily.temperature_2m_max[1]);
              }
              // The watch formatter sinks the readout when any extreme is
              // missing — keep its sentinel predictable by sinking all here too.
              if (high === -999 || low === -999 || lowTmrw === -999 || highTmrw === -999) {
                high = -999;
                low = -999;
                lowTmrw = -999;
                highTmrw = -999;
              }

              // Event hours of the four extremes: each day's argmin/argmax over
              // the hourly curve, grouped by local date (timezone=auto keeps
              // the strings phone-local). Unknown days stay -1 and the watch
              // falls back to a plain LO/HI order.
              var hiHourToday = -1, loHourToday = -1, hiHourTmrw = -1, loHourTmrw = -1;
              if (json.hourly && json.hourly.time && json.hourly.temperature_2m) {
                var hourTemps = json.hourly.temperature_2m;
                var dayOrder = [];
                var dayExtremes = {};
                for (var i = 0; i < json.hourly.time.length; i++) {
                  var hourTemp = hourTemps[i];
                  if (typeof hourTemp !== 'number') continue;
                  var ts = String(json.hourly.time[i]);  // "YYYY-MM-DDTHH:MM"
                  var dayKey = ts.substring(0, 10);
                  if (!dayExtremes[dayKey]) {
                    if (dayOrder.length === 2) continue;
                    dayExtremes[dayKey] = {min: Infinity, minH: -1, max: -Infinity, maxH: -1};
                    dayOrder.push(dayKey);
                  }
                  var hour = parseInt(ts.substring(11, 13), 10);
                  if (isNaN(hour)) continue;
                  var dx = dayExtremes[dayKey];
                  if (hourTemp < dx.min) {
                    dx.min = hourTemp;
                    dx.minH = hour;
                  }
                  if (hourTemp > dx.max) {
                    dx.max = hourTemp;
                    dx.maxH = hour;
                  }
                }
                if (dayOrder.length > 0) {
                  loHourToday = dayExtremes[dayOrder[0]].minH;
                  hiHourToday = dayExtremes[dayOrder[0]].maxH;
                }
                if (dayOrder.length > 1) {
                  loHourTmrw = dayExtremes[dayOrder[1]].minH;
                  hiHourTmrw = dayExtremes[dayOrder[1]].maxH;
                }
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
              forecast = {
                temp: temp,
                cond: cond,
                uv: uv,
                humidity: humidity,
                windDirection: windDirection,
                windSpeed: windSpeed,
                pcp: pcp,
                precipNow: precipNow,
                high: high,
                low: low,
                lowTmrw: lowTmrw,
                highTmrw: highTmrw,
                hiHourToday: hiHourToday,
                loHourToday: loHourToday,
                hiHourTmrw: hiHourTmrw,
                loHourTmrw: loHourTmrw
              };
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
