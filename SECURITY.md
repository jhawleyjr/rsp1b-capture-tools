# Security Policy

## Supported versions

Security fixes are currently provided for the latest code on the `main` branch and the current
`0.1.x` release line after releases begin. Older snapshots are not supported.

## Reporting a vulnerability

Do not disclose suspected vulnerability details in a public issue. Once this repository is public
and GitHub private vulnerability reporting has been enabled, use the
[private security advisory form](https://github.com/jhawleyjr/rsp1b-capture-tools/security/advisories/new).
The form requires the repository's private vulnerability reporting setting to be enabled after
publication; this policy does not claim that the setting is currently enabled.

If the private form is temporarily unavailable, open a minimal public
[contact-request issue](https://github.com/jhawleyjr/rsp1b-capture-tools/issues/new) that asks the
maintainer to establish a private reporting channel. Include no vulnerability description,
reproduction steps, exploit information, attachments, receiver serial numbers, credentials, or
private filesystem paths in that public request.

In the private report, include a clear description, reproduction steps, affected versions, and
potential impact. The maintainer will acknowledge a report when contact is established and will
coordinate validation, remediation, and disclosure as appropriate.

## Scope

This policy covers vulnerabilities in this repository's original source and build configuration. The
proprietary SDRplay API, driver, service or daemon, and firmware are maintained by SDRplay and are
outside this project's security support boundary. Radio-frequency compliance, antenna systems, and
electrical safety—including damage caused by inappropriate Bias-T use—are also outside the software
security boundary.
