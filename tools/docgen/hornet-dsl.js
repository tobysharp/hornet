module.exports = function(hljs) {

  const KEYWORDS = 'Rule Let Require if otherwise in';
  const TYPES = 'Block OpCode int32';

  return {
    name: 'Hornet DSL',
    lexemes: /[A-Za-z_][A-Za-z0-9_]*/,
    keywords: {
      keyword: KEYWORDS,
      type: TYPES
    },

    contains: [
      hljs.COMMENT('//', '$'),

      { className: 'number', begin: /\b\d{1,3}(?:,\d{3})*\b/ },

      {
        className: 'operator',
        begin: /\|\->|->|⎧|⎨|⎩|≤|≥|⧺|∈|∀|Σ|↦|←|==|!=|<=|>=/
      },

      { className: 'property', begin: /\.[a-z][A-Za-z0-9_]*/ },

      // variable rule that *does not* swallow keywords or types
      {
        className: 'variable',
        begin: /\b(?!Rule\b|Let\b|Require\b|if\b|otherwise\b|in\b|Block\b|OpCode\b|int32\b)[a-z][A-Za-z0-9_]*/
      }
    ]
  };
};
