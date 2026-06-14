// Marp config — auto-loaded by Marp for VS Code and marp-cli.
//
// Reuses the highlight.js Hornet grammar that tools/docgen already maintains,
// so ```hornet fenced code blocks in Marp decks get the same github-dark
// highlighting as the docgen HTML/PDF output. (highlight.js lives in the
// docgen workspace, so we require it by explicit path.)
const path = require('path')
const hljs = require(path.join(__dirname, 'tools/docgen/node_modules/highlight.js'))

if (!hljs.getLanguage('hornet')) {
  hljs.registerLanguage('hornet', require(path.join(__dirname, 'tools/docgen/hornet-dsl.js')))
}

// highlight.js never tokenises punctuation, so operators like { } ( ) [ ] < > : ;
// , . :: come out unstyled (base text colour). Wrap punctuation runs in an
// `hljs-operator` span so the theme can colour them — including punctuation inside
// structural spans (hljs-function, hljs-params, …) — but NOT inside literal spans
// (strings, comments, …) where the characters are content, not operators.
const PUNCT = /(?:&(?:lt|gt|amp);|[{}()[\]:;,.=+\-*/%&|^!~?@])+/g
const LITERAL = /hljs-(string|comment|regexp|quote|char|meta-string|doctag|formula)/
function colorizeOperators(html) {
  const stack = []
  let literalDepth = 0
  return html.replace(/<\/?span[^>]*>|[^<]+/g, (tok) => {
    if (tok[0] === '<') {
      if (tok.startsWith('</span')) {
        const cls = stack.pop()
        if (cls && LITERAL.test(cls)) literalDepth--
      } else if (tok.startsWith('<span')) {
        const m = tok.match(/class="([^"]*)"/)
        const cls = m ? m[1] : ''
        stack.push(cls)
        if (LITERAL.test(cls)) literalDepth++
      }
      return tok
    }
    if (literalDepth > 0) return tok
    return tok.replace(PUNCT, (m) => `<span class="hljs-operator">${m}</span>`)
  })
}

module.exports = {
  // Self-contained so `marp --config marp.config.js docs/brink.md` just works.
  allowLocalFiles: true,
  themeSet: [path.join(__dirname, 'docs/marp-hornet.css')],

  // Functional engine: receives Marp's default instance and returns it,
  // with the code highlighter extended to handle the `hornet` language and to
  // colour operator/punctuation tokens that highlight.js leaves bare.
  engine: ({ marp }) => {
    const base = marp.highlighter.bind(marp)
    marp.highlighter = (code, lang, attrs) => {
      const html =
        lang === 'hornet'
          ? hljs.highlight(code, { language: 'hornet', ignoreIllegals: true }).value
          : base(code, lang, attrs)
      return colorizeOperators(html)
    }
    return marp
  },
}
