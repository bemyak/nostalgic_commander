'use strict';

// The Clay settings page as code instead of duplicated JSON: one label map per
// complication, one values array per slot group. The curation — wide
// composites up top, atomic singles at the bottom, wide-only sources in the
// centre — is the three value arrays below. Values are ComplicationDataSource
// ids: stable, persisted, never renumbered (the enum rules in AGENTS.md).

var OPTION_LABELS = {
  20: 'Empty',
  0: 'Battery',
  32: 'Bluetooth + Quiet Time',
  22: 'Short Date (no year)',
  21: '.beat time',
  1: 'Steps',
  2: 'Sleep',
  6: 'Heart Rate',
  10: 'Active Minutes',
  5: 'Weather',
  30: 'Next High / Low temperatures',
  34: 'Wind',
  35: 'Humidity + Precipitation (next 12h max)',
  18: 'AQI UV Index (next 12h max)',
  23: 'Date',
  27: 'Full Weather',
  24: 'Steps Progress',
  25: 'Battery Progress',
  9: 'Bluetooth Status',
  31: 'Quiet Time',
  3: 'Temperature',
  28: 'Precipitation (next 12h max)',
  26: 'Humidity',
  16: 'Air Quality (AQI)',
  17: 'UV Index (next 12h max)',
};

var TOP_VALUES = ['20', '0', '32', '22', '21', '1', '2', '6', '10', '5', '30', '34', '35', '18'];
var CENTER_VALUES = ['23', '27', '24', '25'];
var BOTTOM_VALUES =
    ['20', '0', '9', '31', '21', '1', '2', '6', '10', '3', '28', '34', '26', '16', '17'];
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

function labeledOptions(pairs) {
  return pairs.map(function(pair) { return {value: pair[0], label: pair[1]}; });
}

function slotSelect(messageKey, label, defaultValue, values, overrides) {
  return select(messageKey, label, defaultValue, optionsFor(values, overrides));
}

module.exports = [
  {
    type : 'heading',
    defaultValue : 'Nostalgic Commander Settings',
  },
  {
    type : 'section',
    items :
          [
            select('SETTINGS_THEME', 'Theme', '2', labeledOptions([
                     ['1', 'Turbo Vision (light grey)'], ['2', 'Norton (EGA blue)'],
                     ['3', 'Dark (black)'], ['4', 'Navigator (dark grey)']
                   ])),
            select(
                'SETTINGS_UNITS', 'Units', '0',
                labeledOptions([['0', 'Imperial'], ['1', 'Metric']])),
            select('SETTINGS_DATE_FORMAT', 'Date format', '0', labeledOptions([
                     ['0', 'ISO (1970-12-31)'], ['1', 'DOS (31-12-1970)'],
                     ['2', 'Text (DEC 31st, 1970)'], ['3', 'Short (no year)']
                   ])),
            select(
                'SETTINGS_SHORT_DATE_FORMAT', 'Short date format', '0',
                labeledOptions([['0', 'Month-Day (12-31)'], ['1', 'Day-Month (31-12)']])),
            select('SETTINGS_DOW_POSITION', 'Day of week', '0', labeledOptions([
                     ['0', 'Before date (THU 1970-12-31)'], ['1', 'After date (1970-12-31 THU)'],
                     ['2', 'Hidden (1970-12-31)']
                   ])),
            select(
                'SETTINGS_DISCONNECT_VIBE', 'Enable vibration on phone disconnect', '1',
                labeledOptions([['1', 'On'], ['0', 'Off']])),
            {
              type : 'text',
              defaultValue : 'If disconnects are frequent, your phone is killing the Pebble app in the background - which also breaks notifications and weather. On Android, exclude the app from battery optimization.',
            },
          ],
  },
  {
    type : 'section',
    items :
          [
            {
              type : 'heading',
              defaultValue : 'Layout Settings',
            },
            slotSelect('SLOT_1', 'Top Left Slot', '5', TOP_VALUES),
            slotSelect('SLOT_2', 'Top Right Slot', '2', TOP_VALUES),
            slotSelect('SLOT_6', 'Center Slot', '23', CENTER_VALUES),
            slotSelect('SLOT_3', 'Bottom Left Slot', '1', BOTTOM_VALUES, BOTTOM_LABEL_OVERRIDES),
            slotSelect('SLOT_4', 'Bottom Center Slot', '6', BOTTOM_VALUES, BOTTOM_LABEL_OVERRIDES),
            slotSelect('SLOT_5', 'Bottom Right Slot', '9', BOTTOM_VALUES, BOTTOM_LABEL_OVERRIDES),
          ],
  },
  {
    type : 'submit',
    defaultValue : 'Save Settings',
  },
];
