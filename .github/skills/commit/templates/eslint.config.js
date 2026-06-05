// Rich ESLint flat-config template for the commit skill
// Purpose: a best-practices example that the skill will copy into the
// repository root as `eslint.config.js` when provisioning a default config.
// Update this template as project needs evolve.

module.exports = [
  {
    files: ['**/*.{js,jsx,ts,tsx}'],
    ignores: ['node_modules/**'],
    languageOptions: {
      parser: require('@babel/eslint-parser'),
      parserOptions: {
        ecmaVersion: 2021,
        sourceType: 'module',
        ecmaFeatures: { jsx: true },
        requireConfigFile: false,
        babelOptions: { parserOpts: { plugins: ['jsx'] } },
      },
    },
    plugins: {
      react: require('eslint-plugin-react'),
    },
    rules: {
      // example rules — adapt to your project
      'no-unused-vars': ['warn', { argsIgnorePattern: '^_' }],
      'no-console': ['warn', { allow: ['warn', 'error'] }],
      'react/jsx-uses-vars': 'warn',
      'react/jsx-uses-react': 'warn',
      'react/react-in-jsx-scope': 'warn',
    },
  },
]
