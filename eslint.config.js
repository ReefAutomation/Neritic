import globals from 'globals';
import eslintReact from '@eslint-react/eslint-plugin';

const reactRecommended = eslintReact.configs.recommended;

export default [
  {
    files: ['web/**/*.jsx', 'web/**/*.js'],
    languageOptions: {
      ecmaVersion: 'latest',
      sourceType: 'module',
      parserOptions: {
        ecmaFeatures: {
          jsx: true,
        },
      },
      globals: {
        ...globals.browser,
      },
    },
    plugins: {
      ...reactRecommended.plugins,
    },
    settings: {
      ...reactRecommended.settings,
      react: {
        pragma: 'h',
        version: 'detect',
      },
    },
    rules: {
      ...reactRecommended.rules,
      'no-unused-vars': ['warn', { argsIgnorePattern: '^_' }],
    },
  },
];
