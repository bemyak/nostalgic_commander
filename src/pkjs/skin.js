'use strict';

// Norton Commander skin for the Clay page, injected by index.js's customFn.
// Clay hides the <select> under an opacity-0 overlay and draws span.value
// instead; browsers with customizable selects get the real, styled control.
module.exports = [
  '@font-face{font-family:"Px437 IBM VGA";src:url(data:font/ttf;base64,$$FONT$$) format("truetype")}',
  'html,body{margin:0;background:#0000AA;color:#55FFFF;font:20px/1 "Px437 IBM VGA",monospace;' +
      '-webkit-font-smoothing:none}',
  'html,body{height:100%;overflow:hidden}', 'html body{padding:0}',
  // Starts 1px down so Chromium does not promote it to the root scroller,
  // whose bar the Android WebView draws itself, at the screen edge.
  '#main-form{position:fixed;inset:1px 0 0 0;box-sizing:border-box;overflow-x:hidden;overflow-y:auto;' +
      '-webkit-overflow-scrolling:touch;scrollbar-width:none;' +
      'padding:0 1ch 2em 0}',
  '#main-form::-webkit-scrollbar{display:none}',
  '.nc-scroll{position:fixed;top:1px;right:0;width:1ch;overflow:hidden;touch-action:none;z-index:10}',
  '.nc-scroll span{display:block;height:1em;color:#00AAAA}', '.nc-scroll .on{color:#55FFFF}',
  '*{border-radius:0!important;transition:none!important;font-synthesis:none}',
  'h1,h2,h3,h4,h5,h6,button{font:inherit;font-family:"Px437 IBM VGA",monospace!important;font-weight:400!important;text-transform:none;margin:0;letter-spacing:0}',
  '.section{position:relative;background:#0000AA;border:3px double #55FFFF;box-shadow:none;' +
      'padding:1em;margin:1.5em 1em}',
  '.section>.component{padding:0;margin:0 0 1em}',
  '.section>.component:last-child{margin-bottom:0}',
  '.section>.component:after{display:none!important}',
  '.section>.component-heading:first-child{position:absolute;top:-.5em;left:0;right:0;' +
      'margin:0;background:none;text-align:center;pointer-events:none}',
  '.section>.component-heading:first-child :is(h1,h2,h3,h4,h5,h6){display:inline-block;background:#0000AA;' +
      'padding:0 1ch;color:#55FFFF}',
  '.section.active>.component-heading:first-child :is(h1,h2,h3,h4,h5,h6){background:#55FFFF;color:#0000AA}',
  '.component-select label{display:block;padding:0;color:#FFFFFF}',
  '.component-select .label{display:block}',
  '.component-select .value{background:#00AAAA;color:#000000;padding:0 3ch 0 1ch;text-align:left;overflow:hidden;white-space:nowrap}',
  '.component-select .value:after{content:"\\A0\\2193\\A0";border:0;width:auto;height:auto;margin:0;top:0;' +
      'background:linear-gradient(90deg,#00AAAA .5ch,#000000 .5ch 2.5ch,#00AAAA 2.5ch);color:#FFFFFF}',
  '.component-select select{font:inherit;color:#000000;background:#00AAAA}',
  '.component-radio>.label{display:block;padding:0;color:#FFFFFF}',
  '.component-radio .radio-group{padding:0 0 0 1ch}',
  '.component-radio .radio-group label{padding:0;justify-content:flex-start;color:#55FFFF}',
  '.component-radio .radio-group .label{font-size:inherit}',
  '.component-radio .radio-group i{display:none}',
  '.component-radio .radio-group label:before{content:"( )";white-space:pre;margin-right:1ch}',
  '.component-radio .radio-group label:has(input:checked):before{content:"(\\2022)";color:#FFFF55}',
  '.component-radio .radio-group label:has(input:checked) .label{color:#FFFF55}',
  '.component-radio.disabled>*{opacity:1}',
  '.component-radio.disabled>.label,.component-radio.disabled .radio-group label,' +
      '.component-radio.disabled .radio-group label:before,' +
      '.component-radio.disabled .radio-group label:has(input:checked):before,' +
      '.component-radio.disabled .radio-group label:has(input:checked) .label{color:#AAAAAA}',
  '.component-toggle label{padding:0;justify-content:flex-start;color:#FFFFFF}',
  '.component-toggle .input{display:none}',
  '.component-toggle label:before{content:"[ ]";white-space:pre;margin-right:1ch;color:#55FFFF}',
  '.component-toggle label:has(input:checked):before{content:"[X]";' +
      'background:linear-gradient(90deg,#55FFFF 1ch,#FFFF55 1ch 2ch,#55FFFF 2ch);' +
      '-webkit-background-clip:text;background-clip:text;color:transparent}',
  '.component-slider label{display:block;padding:0;color:#FFFFFF}',
  '.component-slider .label-container{padding:0}', '.component-slider .label{padding:0}',
  '.component-slider .value-wrap,.component-slider .input{display:none}',
  '.nc-slider{display:flex;align-items:center;gap:1ch}',
  '.nc-slider .nc-step{width:1ch;height:1em;line-height:1em;margin:0;padding:0;text-align:left;' +
      'box-shadow:none;transform:none}',
  '.nc-next{text-indent:.125ch}',
  '.nc-slider .nc-step:active{background:#000000;color:#00AAAA;transform:none}',
  '.nc-bar{display:flex;flex:none;line-height:2em;color:#00AAAA;touch-action:pan-y}',
  '.nc-bar span{flex:none;width:1ch}', '.nc-bar .on{color:#55FFFF}',
  '.nc-value{flex:none;width:4ch;text-align:right;line-height:2em;color:#FFFF55}',
  '.component-slider.disabled>*{opacity:1}',
  '.component-slider.disabled .label,.component-slider.disabled .nc-bar span,' +
      '.component-slider.disabled .nc-value{color:#AAAAAA}',
  '.component-slider.disabled .nc-step{background:none;color:#AAAAAA}',
  '.description{color:#AAAAAA;padding:0;font-size:inherit}',
  '.component-button{margin-bottom:0!important}',
  'button,button[type=submit]{font:inherit;text-transform:none;background:#00AAAA;color:#000000;border:0;' +
      'padding:0 1ch;margin:0;min-width:0;box-shadow:2px 2px 0 #000000}',
  'button:active{box-shadow:none;transform:translate(2px,2px)}',
  '.component-button button{margin:1em 0 .5em;line-height:1em;padding:0 2ch;background:#FFFFFF;' +
      'box-shadow:1ch .5em 0 #000000}',
  '.component-button button:active{box-shadow:none}',
  '#main-form>.component-button:has(+.component-submit) button{background:#AA0000;color:#FFFFFF}',
  '.component-submit{position:fixed;left:0;right:0;bottom:0;margin:0;background:#000000;' +
      'padding:0 0 env(safe-area-inset-bottom,0px);text-align:left}',
  '.component-submit:before{content:"C:\\\\>";display:block;color:#AAAAAA;padding-left:1ch}',
  '.component-submit:after{content:"1";position:absolute;left:0;bottom:env(safe-area-inset-bottom,0px);' +
      'color:#FFFFFF;line-height:calc(1.5em + var(--slack,0px))}',
  '.component-submit button,.component-submit button[type=submit]{display:inline-block;margin:-1em 0 0 1ch;' +
      'text-align:left;padding:1em 1ch 0 0;line-height:calc(1.5em + var(--slack,0px));vertical-align:bottom;' +
      'box-shadow:none;background:linear-gradient(#00AAAA,#00AAAA) 0 100%/100% calc(1.5em + var(--slack,0px)) no-repeat}',
  '.component-submit~.component-button{position:fixed;bottom:env(safe-area-inset-bottom,0px);display:flex;' +
      'align-items:flex-end;margin:0;padding:0;background:#000000}',
  '.component-submit~.component-button:before{display:inline-block;line-height:calc(1.5em + var(--slack,0px));' +
      'vertical-align:bottom;color:#FFFFFF}',
  '.component-submit~.component-button button{margin:-1em 0 0;padding:1em 1ch 0 0;' +
      'text-align:left;box-shadow:none;line-height:calc(1.5em + var(--slack,0px));vertical-align:bottom;' +
      'background:linear-gradient(#00AAAA,#00AAAA) 0 100%/100% calc(1.5em + var(--slack,0px)) no-repeat}',
  '.component-submit+.component-button{left:7ch}',
  '.component-submit+.component-button:before{content:"2"}',
  '.component-submit+.component-button+.component-button{right:0}',
  '.component-submit+.component-button+.component-button:before{content:"10"}',
  '@supports (appearance:base-select){', '  .component-select .value{display:none}',
  '  .component-select select,.component-select select::picker(select){appearance:base-select}',
  '  .component-select select{position:relative;opacity:1;width:100%;box-sizing:border-box;border:0;padding:0 3ch 0 1ch;gap:0;display:flex;align-items:center;white-space:nowrap;background:#00AAAA;color:#000000;height:1em;min-height:0;line-height:1em;overflow:hidden;min-width:0;max-width:100%}',
  '  select::picker(select){inline-size:max-content;min-inline-size:auto;position-area:block-end span-inline-start;color:#000000;' +
      'border:solid #000000;border-width:.0625em .25ch;padding:.5em .375ch;margin:.9375em .375ch;background:#00AAAA;' +
      'box-shadow:0 0 0 .375ch #00AAAA,0 .25em 0 .375ch #00AAAA,0 -.25em 0 .375ch #00AAAA,2ch 1em 0 .375ch rgba(0,0,0,.65)}',
  '  select::picker-icon{content:"\\A0\\2193\\A0";position:absolute;top:0;right:0;line-height:1em;' +
      'background:linear-gradient(90deg,#00AAAA .5ch,#000000 .5ch 2.5ch,#00AAAA 2.5ch);color:#FFFFFF}',
  '  select:active::picker-icon{background:linear-gradient(90deg,#000000 .5ch,#00AAAA .5ch 2.5ch,#000000 2.5ch);' +
      'color:#000000}',
  '  select::picker(select){scrollbar-width:thin;scrollbar-color:#000000 #00AAAA}',
  '  select::picker(select)::-webkit-scrollbar{width:1ch;background:#00AAAA}',
  '  select::picker(select)::-webkit-scrollbar-thumb{background:#000000}',
  '  select::picker(select)::-webkit-scrollbar-button{display:none}',
  '  option{background:#00AAAA;color:#000000;padding:0 1ch;font:inherit}',
  '  option:checked,option:hover,option:focus{background:#000000;color:#FFFFFF}',
  '  option::checkmark{content:"\\221A";margin-right:1ch}', '}'
].join('\n');
