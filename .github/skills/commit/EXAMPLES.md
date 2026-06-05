Quick commit

- User: `/commit`
- Assistant: shows status/diff, proposes a one-line header + short body, user replies "Yes" → assistant commits.

Edit message

- User: `/commit`
- Assistant: proposes message, user replies:

  Edit: fix: normalize newline handling in parser

  Fixes inconsistent CRLF behavior across platforms.

  → assistant commits with the edited message.
