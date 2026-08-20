# Separate browser sessions and automation tokens

Browser users will authenticate with the Management Credential and receive a
short-lived secure session, while external clients use separately generated,
revocable Automation Tokens that are shown only once. Physical recovery
invalidates all sessions and tokens, preventing scripts from requiring or
retaining the administrator password.
