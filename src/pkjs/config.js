'use strict';

// The Clay settings page as code instead of duplicated JSON: one label map per
// complication, one values array per slot group. The curation — wide
// composites up top, atomic singles at the bottom, wide-only sources in the
// centre — is the three value arrays below. Values are ComplicationDataSource
// ids: stable, persisted, never renumbered (the enum rules in AGENTS.md).

var OPTION_LABELS = {
  20: 'Empty',
  0: 'Battery',
  32: 'Bluetooth + quiet time',
  22: 'Short date',
  21: '.beat time',
  1: 'Steps',
  2: 'Sleep',
  6: 'Heart rate',
  10: 'Active minutes',
  5: 'Weather',
  30: 'Next high/low',
  34: 'Wind',
  35: 'Humidity + precipitation',
  18: 'AQI + UV index',
  23: 'Date',
  27: 'Full weather',
  24: 'Steps progress',
  25: 'Battery progress',
  9: 'Bluetooth status',
  31: 'Quiet time',
  3: 'Temperature',
  28: 'Precipitation',
  26: 'Humidity',
  16: 'Air quality (AQI)',
  17: 'UV index',
  33: 'Week number',
  36: 'Solar hour angle',
  37: 'Solar elevation',
  38: 'Solar hour angle & elevation',
};

var TOP_VALUES =
    ['20', '0', '32', '22', '21', '1', '2', '6', '10', '5', '30', '34', '35', '18', '38'];
var CENTER_VALUES = ['23', '27', '24', '25'];
var BOTTOM_VALUES = [
  '20', '0', '9', '31', '22', '21', '1', '2', '6', '10', '3', '28', '34', '26', '16', '17', '33',
  '36', '37'
];
// The narrow bottom slots have no unit-bearing caption stub; wind's units ride
// its label there.
var BOTTOM_LABEL_OVERRIDES = {34: 'Wind (m/s or mph)'};

function optionsFor(values, overrides) {
  return values.map(function(value) {
    return {value: value, label: (overrides && overrides[value]) || OPTION_LABELS[value]};
  });
}

function select(messageKey, label, defaultValue, options) {
  return {
    type: 'select',
    messageKey: messageKey,
    label: label,
    defaultValue: defaultValue,
    options: options,
  };
}

function radiogroup(messageKey, label, defaultValue, options, description) {
  var item = {
    type: 'radiogroup',
    messageKey: messageKey,
    label: label,
    defaultValue: defaultValue,
    options: options,
  };
  if (description) item.description = description;
  return item;
}

function slider(messageKey, label, defaultValue, description) {
  var item = {
    type: 'slider',
    messageKey: messageKey,
    label: label,
    min: 0,
    max: 100,
    step: 1,
    defaultValue: defaultValue,
  };
  if (description) item.description = description;
  return item;
}

function toggle(messageKey, label, description) {
  var item = {
    type: 'toggle',
    messageKey: messageKey,
    label: label,
    defaultValue: false,
  };
  if (description) item.description = description;
  return item;
}

function labeledOptions(pairs) {
  return pairs.map(function(pair) { return {value: pair[0], label: pair[1]}; });
}

function slotSelect(messageKey, label, defaultValue, values, overrides) {
  return select(messageKey, label, defaultValue, optionsFor(values, overrides));
}

module.exports = [
  {
    type : 'section',
    items :
          [
            {
              type : 'heading',
              defaultValue : 'General',
            },
            radiogroup(
                'SETTINGS_THEME', 'Theme', '2', labeledOptions([
                  ['2', 'Norton (blue)'], ['1', 'Turbo Vision (light grey)'],
                  ['3', 'Console (black)'], ['4', 'Navigator (dark grey)'], ['6', 'Amber (maroon)']
                ])),
            select(
                'SETTINGS_NIGHT_THEME', 'Night theme', '0', labeledOptions([
                  ['0', 'Off'], ['2', 'Norton (blue)'], ['1', 'Turbo Vision (light grey)'],
                  ['3', 'Console (black)'], ['4', 'Navigator (dark grey)'], ['6', 'Amber (maroon)']
                ])),
            radiogroup(
                'SETTINGS_UNITS', 'Units', '0',
                labeledOptions([['0', 'Imperial'], ['1', 'Metric']])),
            radiogroup(
                'SETTINGS_WEATHER_WINDOW', 'Forecast window', '12', labeledOptions([
                  ['0', 'Now'], ['2', '2 hours'], ['8', '8 hours'], ['12', '12 hours'],
                  ['24', '24 hours']
                ]),
                'Precipitation and UV show the window\'s maximum'),
            radiogroup('SETTINGS_DATE_FORMAT', 'Date format', '0', labeledOptions([
                         ['0', '1970-12-31'], ['1', '31-12-1970'], ['2', 'DEC 31st, 1970'],
                         ['3', 'No year']
                       ])),
            radiogroup(
                'SETTINGS_SHORT_DATE_FORMAT', 'Short date format', '0',
                labeledOptions([['0', '12-31'], ['1', '31-12']])),
            radiogroup('SETTINGS_DOW_POSITION', 'Day of week', '0', labeledOptions([
                         ['0', 'THU 1970-12-31'], ['1', '1970-12-31 THU'], ['2', 'Hidden']
                       ])),
          ],
  },
  {
    type : 'section',
    items :
          [
            {
              type : 'heading',
              defaultValue : 'Effects',
            },
            toggle(
                'SETTINGS_CRT_ABERRATION', 'Chromatic aberration',
                'Splits the colours toward the edges'),
            toggle(
                'SETTINGS_CRT_CURVATURE', 'Screen curvature',
                'Bends the picture, darkens the corners'),
            toggle(
                'SETTINGS_CRT_DEGAUSS', 'Degauss on wake',
                'Shakes the picture when the backlight turns on'),
            slider('SETTINGS_DEGAUSS_VOLUME', 'Degauss volume', 10),
            slider('SETTINGS_CHIME_VOLUME', 'Hourly chime', 0, 'A POST beep on the hour'),
            // The test button's flag. Keyed so Clay serializes it (the input
            // manipulator is .val); hidden on the page by index.js's customFn.
            {
              type : 'input',
              messageKey : 'CHIME_TEST',
              defaultValue : '0',
            },
            // No messageKey here, on purpose: the button manipulator's value
            // is the button's innerHTML — a key would send the caption text
            // to the watch. index.js's customFn wires the click instead.
            {
              type : 'button',
              id : 'test-sound',
              defaultValue : 'Speaker test',
              description : 'Closes the page and beeps at the chime volume',
            },
          ],
  },
  {
    type : 'section',
    items :
          [
            {
              type : 'heading',
              defaultValue : 'Layout',
            },
            slotSelect('SLOT_1', 'Top left ↖', '5', TOP_VALUES),
            slotSelect('SLOT_2', 'Top right ↗', '2', TOP_VALUES),
            slotSelect('SLOT_6', 'Middle ■', '23', CENTER_VALUES),
            slotSelect('SLOT_3', 'Bottom left ↙', '1', BOTTOM_VALUES, BOTTOM_LABEL_OVERRIDES),
            slotSelect('SLOT_4', 'Bottom middle ↓', '6', BOTTOM_VALUES, BOTTOM_LABEL_OVERRIDES),
            slotSelect('SLOT_5', 'Bottom right ↘', '9', BOTTOM_VALUES, BOTTOM_LABEL_OVERRIDES),
          ],
  },
  // No messageKey, like test-sound; index.js's customFn submits the form.
  {
    type : 'button',
    id : 'save-inline',
    defaultValue : 'Save',
  },
  {
    type : 'submit',
    defaultValue : 'Save',
  },
  // No messageKey, like test-sound; index.js's customFn resets the form.
  {
    type : 'button',
    id : 'defaults',
    defaultValue : 'Defaults',
  },
  // No messageKey, for the same reason as test-sound; index.js's customFn
  // wires the click to close the page without a payload.
  {
    type : 'button',
    id : 'quit',
    defaultValue : 'Quit',
  },
];
