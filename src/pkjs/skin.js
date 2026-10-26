'use strict';

// Norton Commander skin for the Clay page, injected by index.js's customFn.
// Colours are per-theme palettes of role-named custom properties, selected by
// body[data-theme] (the SETTINGS_THEME value; Norton is also the fallback).
// Clay hides the <select> under an opacity-0 overlay and draws span.value
// instead; browsers with customizable selects get the real, styled control.
module.exports = [
  '@font-face{font-family:"Px437 IBM VGA";src:url(data:font/ttf;base64,$$FONT$$) format("truetype")}',
  // Norton
  'body,body[data-theme="2"]{' +
      '--bg:#0000AA;--frame:#55FFFF;--frame-ink:#0000AA;--heading:#55FFFF;' +
      '--text:#55FFFF;--hilite:#FFFFFF;--mark:#FFFF55;--muted:#AAAAAA;' +
      '--disabled:#AAAAAA;--dim:#00AAAA;--control-bg:#00AAAA;--control-ink:#000000;' +
      '--cursor-bg:#000000;--cursor-ink:#FFFFFF;--button-bg:#FFFFFF;--danger-bg:#AA0000;' +
      '--danger-ink:#FFFFFF;--shadow:#000000;--bar-bg:#000000;--bar-hint:#AAAAAA;' +
      '--bar-key:#FFFFFF}',
  // Turbo Vision
  'body[data-theme="1"]{' +
      '--bg:#AAAAAA;--frame:#555555;--frame-ink:#FFFFFF;--heading:#000000;' +
      '--text:#000000;--hilite:#000000;--mark:#AA0000;--muted:#000000;' +
      '--disabled:#555555;--dim:#555555;--control-bg:#00AA00;--control-ink:#000000;' +
      '--cursor-bg:#000000;--cursor-ink:#FFFFFF;--button-bg:#FFFFFF;--danger-bg:#AA0000;' +
      '--danger-ink:#FFFFFF;--shadow:#000000;--bar-bg:#000000;--bar-hint:#AAAAAA;' +
      '--bar-key:#FFFFFF}',
  // Console
  'body[data-theme="3"]{' +
      '--bg:#000000;--frame:#555555;--frame-ink:#FFFFFF;--heading:#AAAAAA;' +
      '--text:#AAAAAA;--hilite:#FFFFFF;--mark:#FFFF55;--muted:#AAAAAA;' +
      '--disabled:#555555;--dim:#555555;--control-bg:#AAAAAA;--control-ink:#000000;' +
      '--cursor-bg:#000000;--cursor-ink:#FFFFFF;--button-bg:#FFFFFF;--danger-bg:#AA0000;' +
      '--danger-ink:#FFFFFF;--shadow:#555555;--bar-bg:#000000;--bar-hint:#AAAAAA;' +
      '--bar-key:#FFFFFF}',
  // Navigator
  'body[data-theme="4"]{' +
      '--bg:#555555;--frame:#FFFFFF;--frame-ink:#555555;--heading:#FFFFFF;' +
      '--text:#FFFFFF;--hilite:#FFFF55;--mark:#55FFFF;--muted:#FFFFFF;' +
      '--disabled:#AAAAAA;--dim:#AAAAAA;--control-bg:#AAAAAA;--control-ink:#000000;' +
      '--cursor-bg:#000000;--cursor-ink:#FFFFFF;--button-bg:#FFFFFF;--danger-bg:#AA0000;' +
      '--danger-ink:#FFFFFF;--shadow:#000000;--bar-bg:#000000;--bar-hint:#AAAAAA;' +
      '--bar-key:#FFFFFF}',
  // Amber
  'body[data-theme="6"]{' +
      '--bg:#550000;--frame:#FFAA00;--frame-ink:#550000;--heading:#FFAA00;' +
      '--text:#FFAA00;--hilite:#FFFFFF;--mark:#FFFF55;--muted:#AAAAAA;' +
      '--disabled:#AAAAAA;--dim:#AA5500;--control-bg:#FFAA00;--control-ink:#000000;' +
      '--cursor-bg:#000000;--cursor-ink:#FFFFFF;--button-bg:#FFFFFF;--danger-bg:#AA0000;' +
      '--danger-ink:#FFFFFF;--shadow:#000000;--bar-bg:#000000;--bar-hint:#AAAAAA;' +
      '--bar-key:#FFFFFF}',
  'html,body{margin:0;font:20px/1 "Px437 IBM VGA",monospace;-webkit-font-smoothing:none}',
  'body{background:var(--bg);color:var(--text)}', 'html,body{height:100%;overflow:hidden}',
  'html body{padding:0}',
  // Starts 1px down so Chromium does not promote it to the root scroller,
  // whose bar the Android WebView draws itself, at the screen edge.
  '#main-form{position:fixed;inset:1px 0 0 0;box-sizing:border-box;overflow-x:hidden;overflow-y:auto;' +
      '-webkit-overflow-scrolling:touch;scrollbar-width:none;' +
      'padding:0 1ch 2em 0}',
  '#main-form::-webkit-scrollbar{display:none}',
  '.nc-scroll{position:fixed;top:1px;right:0;width:1ch;overflow:hidden;touch-action:none;z-index:10}',
  '.nc-scroll span{display:block;height:1em;color:var(--dim)}', '.nc-scroll .on{color:var(--text)}',
  '*{border-radius:0!important;transition:none!important;font-synthesis:none}',
  'h1,h2,h3,h4,h5,h6,button{font:inherit;font-family:"Px437 IBM VGA",monospace!important;font-weight:400!important;text-transform:none;margin:0;letter-spacing:0}',
  '.section{position:relative;background:var(--bg);border:3px double var(--frame);box-shadow:none;' +
      'padding:1em;margin:1.5em 1em}',
  '.section>.component{padding:0;margin:0 0 1em}',
  '.section>.component:last-child{margin-bottom:0}',
  '.section>.component:after{display:none!important}',
  '.section>.component-heading:first-child{position:absolute;top:-.5em;left:0;right:0;' +
      'margin:0;background:none;text-align:center;pointer-events:none}',
  '.section>.component-heading:first-child :is(h1,h2,h3,h4,h5,h6){display:inline-block;background:var(--bg);' +
      'padding:0 1ch;color:var(--heading)}',
  '.section.active>.component-heading:first-child :is(h1,h2,h3,h4,h5,h6){background:var(--frame);color:var(--frame-ink)}',
  '.component-select label{display:block;padding:0;color:var(--hilite)}',
  '.component-select .label{display:block}',
  '.component-select .value{background:var(--control-bg);color:var(--control-ink);padding:0 3ch 0 1ch;text-align:left;overflow:hidden;white-space:nowrap}',
  '.component-select .value:after{content:"\\A0\\2193\\A0";border:0;width:auto;height:auto;margin:0;top:0;' +
      'background:linear-gradient(90deg,var(--control-bg) .5ch,var(--cursor-bg) .5ch 2.5ch,var(--control-bg) 2.5ch);color:var(--cursor-ink)}',
  '.component-select select{font:inherit;color:var(--control-ink);background:var(--control-bg)}',
  '.component-radio>.label{display:block;padding:0;color:var(--hilite)}',
  '.component-radio .radio-group{padding:0 0 0 1ch}',
  '.component-radio .radio-group label{padding:0;justify-content:flex-start;color:var(--text)}',
  '.component-radio .radio-group .label{font-size:inherit}',
  '.component-radio .radio-group i{display:none}',
  '.component-radio .radio-group label:before{content:"( )";white-space:pre;margin-right:1ch}',
  '.component-radio .radio-group label:has(input:checked):before{content:"(\\2022)";color:var(--mark)}',
  '.component-radio .radio-group label:has(input:checked) .label{color:var(--mark)}',
  '.component-radio.disabled>*{opacity:1}',
  '.component-radio.disabled>.label,.component-radio.disabled .radio-group label,' +
      '.component-radio.disabled .radio-group label:before,' +
      '.component-radio.disabled .radio-group label:has(input:checked):before,' +
      '.component-radio.disabled .radio-group label:has(input:checked) .label{color:var(--disabled)}',
  '.component-toggle label{padding:0;justify-content:flex-start;color:var(--hilite)}',
  '.component-toggle .input{display:none}',
  '.component-toggle label:before{content:"[ ]";white-space:pre;margin-right:1ch;color:var(--text)}',
  '.component-toggle label:has(input:checked):before{content:"[X]";' +
      'background:linear-gradient(90deg,var(--text) 1ch,var(--mark) 1ch 2ch,var(--text) 2ch);' +
      '-webkit-background-clip:text;background-clip:text;color:transparent}',
  '.component-slider label{display:block;padding:0;color:var(--hilite)}',
  '.component-slider .label-container{padding:0}', '.component-slider .label{padding:0}',
  '.component-slider .value-wrap,.component-slider .input{display:none}',
  '.nc-slider{display:flex;align-items:center;gap:1ch}',
  '.nc-slider .nc-step{width:1ch;height:1em;line-height:1em;margin:0;padding:0;text-align:left;' +
      'box-shadow:none;transform:none}',
  '.nc-next{text-indent:.125ch}',
  '.nc-slider .nc-step:active{background:var(--cursor-bg);color:var(--control-bg);transform:none}',
  '.nc-bar{display:flex;flex:none;line-height:2em;color:var(--dim);touch-action:pan-y}',
  '.nc-bar span{flex:none;width:1ch}', '.nc-bar .on{color:var(--text)}',
  '.nc-value{flex:none;width:4ch;text-align:right;line-height:2em;color:var(--mark)}',
  '.component-slider.disabled>*{opacity:1}',
  '.component-slider.disabled .label,.component-slider.disabled .nc-bar span,' +
      '.component-slider.disabled .nc-value{color:var(--disabled)}',
  '.component-slider.disabled .nc-step{background:none;color:var(--disabled)}',
  '.description{color:var(--muted);padding:0;font-size:inherit}',
  '.component-button{margin-bottom:0!important}',
  'button,button[type=submit]{font:inherit;text-transform:none;background:var(--control-bg);color:var(--control-ink);border:0;' +
      'padding:0 1ch;margin:0;min-width:0;box-shadow:2px 2px 0 var(--shadow)}',
  'button:active{box-shadow:none;transform:translate(2px,2px)}',
  '.component-button button{margin:1em 0 .5em;line-height:1em;padding:0 2ch;background:var(--button-bg);' +
      'box-shadow:1ch .5em 0 var(--shadow)}',
  '.component-button button:active{box-shadow:none}',
  '#main-form>.component-button:has(+.component-submit) button{background:var(--danger-bg);color:var(--danger-ink)}',
  '.component-submit{position:fixed;left:0;right:0;bottom:0;margin:0;background:var(--bar-bg);' +
      'padding:0 0 env(safe-area-inset-bottom,0px);text-align:left}',
  '.component-submit:before{content:"C:\\\\>";display:block;color:var(--bar-hint);padding-left:1ch}',
  '.component-submit:after{content:"1";position:absolute;left:0;bottom:env(safe-area-inset-bottom,0px);' +
      'color:var(--bar-key);line-height:calc(1.5em + var(--slack,0px))}',
  '.component-submit button,.component-submit button[type=submit]{display:inline-block;margin:-1em 0 0 1ch;' +
      'text-align:left;padding:1em 1ch 0 0;line-height:calc(1.5em + var(--slack,0px));vertical-align:bottom;' +
      'box-shadow:none;background:linear-gradient(var(--control-bg),var(--control-bg)) 0 100%/100% calc(1.5em + var(--slack,0px)) no-repeat}',
  '.component-submit~.component-button{position:fixed;bottom:env(safe-area-inset-bottom,0px);display:flex;' +
      'align-items:flex-end;margin:0;padding:0;background:var(--bar-bg)}',
  '.component-submit~.component-button:before{display:inline-block;line-height:calc(1.5em + var(--slack,0px));' +
      'vertical-align:bottom;color:var(--bar-key)}',
  '.component-submit~.component-button button{margin:-1em 0 0;padding:1em 1ch 0 0;' +
      'text-align:left;box-shadow:none;line-height:calc(1.5em + var(--slack,0px));vertical-align:bottom;' +
      'background:linear-gradient(var(--control-bg),var(--control-bg)) 0 100%/100% calc(1.5em + var(--slack,0px)) no-repeat}',
  '.component-submit+.component-button{left:7ch}',
  '.component-submit+.component-button:before{content:"2"}',
  '.component-submit+.component-button+.component-button{right:0}',
  '.component-submit+.component-button+.component-button:before{content:"10"}',
  '@supports (appearance:base-select){', '  .component-select .value{display:none}',
  '  .component-select select,.component-select select::picker(select){appearance:base-select}',
  '  .component-select select{position:relative;opacity:1;width:100%;box-sizing:border-box;border:0;padding:0 3ch 0 1ch;gap:0;display:flex;align-items:center;white-space:nowrap;background:var(--control-bg);color:var(--control-ink);height:1em;min-height:0;line-height:1em;overflow:hidden;min-width:0;max-width:100%}',
  '  select::picker(select){inline-size:max-content;min-inline-size:auto;position-area:block-end span-inline-start;color:var(--control-ink);' +
      'border:solid var(--shadow);border-width:.0625em .25ch;padding:.5em .375ch;margin:.9375em .375ch;background:var(--control-bg);' +
      'box-shadow:0 0 0 .375ch var(--control-bg),0 .25em 0 .375ch var(--control-bg),0 -.25em 0 .375ch var(--control-bg),2ch 1em 0 .375ch rgba(0,0,0,.65)}',
  '  select::picker-icon{content:"\\A0\\2193\\A0";position:absolute;top:0;right:0;line-height:1em;' +
      'background:linear-gradient(90deg,var(--control-bg) .5ch,var(--cursor-bg) .5ch 2.5ch,var(--control-bg) 2.5ch);color:var(--cursor-ink)}',
  '  select:active::picker-icon{background:linear-gradient(90deg,var(--cursor-bg) .5ch,var(--control-bg) .5ch 2.5ch,var(--cursor-bg) 2.5ch);' +
      'color:var(--cursor-bg)}',
  '  select::picker(select){scrollbar-width:thin;scrollbar-color:var(--cursor-bg) var(--control-bg)}',
  '  select::picker(select)::-webkit-scrollbar{width:1ch;background:var(--control-bg)}',
  '  select::picker(select)::-webkit-scrollbar-thumb{background:var(--cursor-bg)}',
  '  select::picker(select)::-webkit-scrollbar-button{display:none}',
  '  option{background:var(--control-bg);color:var(--control-ink);padding:0 1ch;font:inherit}',
  '  option:checked,option:hover,option:focus{background:var(--cursor-bg);color:var(--cursor-ink)}',
  '  option::checkmark{content:"\\221A";margin-right:1ch}', '}'
].join('\n');
