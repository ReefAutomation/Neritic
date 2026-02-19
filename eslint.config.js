import eslintConfigPreact from 'eslint-config-preact';
import globals from 'globals';

export default [
  ...eslintConfigPreact,
  {
    files: ['web/**/*.jsx', 'web/**/*.js'],
    languageOptions: {
      globals: {
        ...globals.browser,
      },
    },
    rules: {
      'react/prop-types': 'off',
      'react/no-unknown-property': 'off',
      'react/react-in-jsx-scope': 'off',
    },
    settings: {
      react: {
        pragma: 'h',
        version: 'detect',
      },
    },
  },
];
