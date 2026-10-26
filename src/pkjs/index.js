var Clay = require('@rebble/clay');
var clayConfig = require('./config.js');
var weather = require('./weather.js');
var skin = require('./skin.js');
var vgaFont = require('./vga_font.json');

// Injected into the config page via toString (self-contained bodies only;
// `this` is the in-page ClayConfig): apply the skin, hide the CHIME_TEST flag
// input, grey the degauss volume while the strike is off, wire the buttons (the test button raises
// the flag and submits, so the ordinary webviewclosed path carries it to the watch), and mark the
// panel in view as active.
function customFn(minified) {
  var clayConfig = this;
  var style = document.createElement('style');
  style.textContent = clayConfig.meta.userData.css;
  document.head.appendChild(style);
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    clayConfig.getItemByMessageKey('CHIME_TEST').hide();
    var degauss = clayConfig.getItemByMessageKey('SETTINGS_CRT_DEGAUSS');
    var degaussVolume = clayConfig.getItemByMessageKey('SETTINGS_DEGAUSS_VOLUME');
    function syncDegaussVolume() {
      if (degauss.get()) {
        degaussVolume.enable();
      } else {
        degaussVolume.disable();
      }
    }
    degauss.on('change', syncDegaussVolume);
    syncDegaussVolume();
    clayConfig.getItemById('test-sound').on('click', function() {
      clayConfig.getItemByMessageKey('CHIME_TEST').set('1');
      minified.$('#main-form').trigger('submit');
    });
    clayConfig.getItemById('save-inline')
        .on('click', function() { minified.$('#main-form').trigger('submit'); });
    clayConfig.getItemById('defaults').on('click', function() {
      clayConfig.getAllItems().forEach(function(item) {
        if (!item.messageKey || item.messageKey === 'CHIME_TEST') return;
        if (item.config.defaultValue !== undefined) item.set(item.config.defaultValue);
      });
    });
    clayConfig.getItemsByType('slider').forEach(function(item) {
      var CELLS = 10;
      var min = item.config.min;
      var max = item.config.max;
      var unit = (max - min) / CELLS;
      var root = item.$element[0];
      var widget = document.createElement('div');
      widget.className = 'nc-slider';
      function button(glyph, direction, delta) {
        var el = document.createElement('button');
        el.type = 'button';
        el.className = 'nc-step nc-' + direction;
        el.textContent = glyph;
        el.addEventListener('click', function() { setValue(item.get() + delta); });
        return el;
      }
      var bar = document.createElement('span');
      bar.className = 'nc-bar';
      var cells = [];
      for (var k = 0; k < CELLS; k++) {
        cells.push(bar.appendChild(document.createElement('span')));
      }
      var out = document.createElement('span');
      out.className = 'nc-value';
      widget.appendChild(button('\u25C4', 'prev', -item.config.step));
      widget.appendChild(bar);
      widget.appendChild(out);
      widget.appendChild(button('\u25BA', 'next', item.config.step));
      root.insertBefore(widget, root.querySelector('label').nextSibling);

      function setValue(n) {
        if (root.classList.contains('disabled')) return;
        item.set(Math.max(min, Math.min(max, Math.round(n))));
      }
      function render() {
        var value = item.get();
        cells.forEach(function(cell, i) {
          var on = min + (i + 1) * unit <= value;
          cell.textContent = on ? '\u2588' : '\u2591';
          cell.className = on ? 'on' : '';
        });
        out.textContent = value > min ? value + '%' : 'Off';
      }
      function setFromPointer(e) {
        var rect = bar.getBoundingClientRect();
        var cell = Math.ceil((e.clientX - rect.left) / (rect.width / CELLS));
        setValue(min + Math.max(0, Math.min(CELLS, cell)) * unit);
      }
      var pressed = false;
      bar.addEventListener('pointerdown', function(e) {
        pressed = true;
        try {
          bar.setPointerCapture(e.pointerId);
        } catch (err) {
        }
        setFromPointer(e);
      });
      bar.addEventListener('pointermove', function(e) {
        if (pressed) setFromPointer(e);
      });
      bar.addEventListener('pointerup', function() { pressed = false; });
      bar.addEventListener('pointercancel', function() { pressed = false; });
      item.on('change', render);
      render();
    });
    var pane = document.getElementById('main-form');
    var sections = document.querySelectorAll('.section');
    var activeQueued = false;
    function updateActiveSection() {
      activeQueued = false;
      var line = pane.clientHeight / 3;
      var best = null;
      var bestDistance = Infinity;
      Array.prototype.forEach.call(sections, function(section) {
        var rect = section.getBoundingClientRect();
        var distance = Math.max(rect.top - line, line - rect.bottom, 0);
        if (distance < bestDistance) {
          best = section;
          bestDistance = distance;
        }
      });
      Array.prototype.forEach.call(
          sections, function(section) { section.classList.toggle('active', section === best); });
    }
    function queueActiveSection() {
      if (activeQueued) return;
      activeQueued = true;
      requestAnimationFrame(updateActiveSection);
    }
    pane.addEventListener('scroll', queueActiveSection);
    window.addEventListener('resize', queueActiveSection);
    updateActiveSection();
    var bar = document.createElement('div');
    bar.className = 'nc-scroll';
    document.body.appendChild(bar);
    var barCells = [];
    var barQueued = false;
    var barDrag = false;
    function cellHeight() { return parseFloat(getComputedStyle(document.body).fontSize); }
    function maxScroll() { return pane.scrollHeight - pane.clientHeight; }
    function layout() {
      var cell = cellHeight();
      var footer = document.querySelector('.component-submit');
      var inset = parseFloat(getComputedStyle(footer).paddingBottom) || 0;
      var height = window.innerHeight;
      var rows = Math.floor((height - 1 - 2.5 * cell - inset) / cell);
      var mainBottom = 1 + rows * cell;
      pane.style.bottom = height - mainBottom + 'px';
      footer.style.top = mainBottom + 'px';
      document.documentElement.style.setProperty(
          '--slack', height - mainBottom - 2.5 * cell - inset + 'px');
      return rows;
    }
    function thumbRow(rows) {
      return 1 + Math.round(Math.max(0, Math.min(1, pane.scrollTop / maxScroll())) * (rows - 3));
    }
    function drawBar() {
      barQueued = false;
      var rows = layout();
      if (maxScroll() <= 0 || rows < 4) {
        bar.style.display = 'none';
        return;
      }
      bar.style.display = 'block';
      bar.style.height = rows * cellHeight() + 'px';
      while (barCells.length < rows) barCells.push(bar.appendChild(document.createElement('span')));
      while (barCells.length > rows) bar.removeChild(barCells.pop());
      var thumb = thumbRow(rows);
      barCells.forEach(function(cell, i) {
        var arrow = i === 0 || i === rows - 1;
        cell.textContent = i === 0 ? '\u25B2' :
            i === rows - 1         ? '\u25BC' :
            i === thumb            ? '\u25A0' :
                                     '\u2592';
        cell.className = arrow || i === thumb ? 'on' : '';
      });
    }
    function queueBar() {
      if (barQueued) return;
      barQueued = true;
      requestAnimationFrame(drawBar);
    }
    function barRow(e) {
      return Math.floor((e.clientY - bar.getBoundingClientRect().top) / cellHeight());
    }
    function dragTo(e) {
      var rows = Math.floor(bar.getBoundingClientRect().height / cellHeight());
      var row = Math.max(1, Math.min(rows - 2, barRow(e)));
      pane.scrollTop = (row - 1) / (rows - 3) * maxScroll();
    }
    bar.addEventListener('pointerdown', function(e) {
      var rows = Math.floor(bar.getBoundingClientRect().height / cellHeight());
      var row = barRow(e);
      if (row <= 0) {
        pane.scrollTop -= cellHeight();
      } else if (row >= rows - 1) {
        pane.scrollTop += cellHeight();
      } else if (row === thumbRow(rows)) {
        barDrag = true;
        try {
          bar.setPointerCapture(e.pointerId);
        } catch (err) {
        }
      } else {
        pane.scrollTop += row < thumbRow(rows) ? -pane.clientHeight : pane.clientHeight;
      }
    });
    bar.addEventListener('pointermove', function(e) {
      if (barDrag) dragTo(e);
    });
    bar.addEventListener('pointerup', function() { barDrag = false; });
    bar.addEventListener('pointercancel', function() { barDrag = false; });
    pane.addEventListener('scroll', queueBar);
    window.addEventListener('resize', queueBar);
    drawBar();
    clayConfig.getItemById('quit').on('click', function() { location.href = window.returnTo; });
  });
}

var clay = new Clay(clayConfig, customFn, {
  userData: {css: skin.replace('$$FONT$$', vgaFont.ttf)},
});

// The old CRT, degauss sound and hourly chime switches became the effect
// toggles and the two volumes; the watch migrates its own copy the same way.
// Carry the stored page values over once, or the page would show (and Save
// would send) a state the watch does not hold. Stored as booleans or as the
// '0'/'1' strings of the select era.
var storedSettings = readClaySettings();
function wasOn(key) { return storedSettings[key] === true || storedSettings[key] === '1'; }
if ('SETTINGS_CRT' in storedSettings) {
  ['SETTINGS_CRT_ABERRATION', 'SETTINGS_CRT_CURVATURE', 'SETTINGS_CRT_DEGAUSS'].forEach(function(
      key) {
    if (!(key in storedSettings)) clay.setSettings(key, wasOn('SETTINGS_CRT'));
  });
}
if ('SETTINGS_CRT_SOUND' in storedSettings && !('SETTINGS_DEGAUSS_VOLUME' in storedSettings)) {
  clay.setSettings('SETTINGS_DEGAUSS_VOLUME', wasOn('SETTINGS_CRT_SOUND') ? 10 : 0);
}
if ('SETTINGS_HOURLY_CHIME' in storedSettings && !wasOn('SETTINGS_HOURLY_CHIME')) {
  clay.setSettings('SETTINGS_CHIME_VOLUME', '0');
}
['SETTINGS_CRT', 'SETTINGS_CRT_SOUND', 'SETTINGS_HOURLY_CHIME'].forEach(function(
    key) { clay.setSettings(key, undefined); });

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
          forecast.LONGITUDE = weather.coordinateCenti(lon);
          forecast.LATITUDE = weather.coordinateCenti(lat);
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
