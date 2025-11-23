// config.js
const hljs = require('highlight.js');
const hornetDsl = require('./hornet-dsl.js');   // ← IMPORT YOUR GRAMMAR

// Register the Hornet DSL under the language name "hornet"
hljs.registerLanguage('hornet', hornetDsl);     // ← CRITICAL LINE

module.exports = {
  stylesheet: [
    'https://cdnjs.cloudflare.com/ajax/libs/github-markdown-css/5.2.0/github-markdown-dark.min.css',
    'https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github-dark.min.css'
  ],

  body_class: 'markdown-body',
  highlight_style: 'github-dark',

  marked_options: {
    highlight: (code, lang) => {
      // our custom DSL
      if (lang === 'hornet') {
        return hljs.highlight(code, {
          language: 'hornet',
          ignoreIllegals: true
        }).value;
      }

      // fallback for other known languages
      if (lang && hljs.getLanguage(lang)) {
        return hljs.highlight(code, {
          language: lang,
          ignoreIllegals: true
        }).value;
      }

      // auto-detect fallback
      return hljs.highlightAuto(code).value;
    }
  }
};
