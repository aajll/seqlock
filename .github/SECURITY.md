# Security Policy

## Reporting a Vulnerability

Use GitHub's Security Advisories feature to report security concerns
privately.

Expect a response within 7 days. If the issue is confirmed, a fix
will be released as a patch version.

## Scope

seqlock is a small sequence lock library with no network stack, no
external dependencies, and no dynamic memory allocation. The primary
attack surface is race conditions in the single-writer/multi-reader
contract and integer overflow in sequence counter calculations.
