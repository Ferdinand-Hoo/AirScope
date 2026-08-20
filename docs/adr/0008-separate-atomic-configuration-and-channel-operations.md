# Separate atomic configuration and channel operations

Non-channel changes are validated and applied as one Configuration Transaction,
while live channel changes use a separate Channel Switch Operation that always
executes CSA. Neither operation is persisted until runtime application
succeeds, failures retain the previous valid state, and the Management
Interface does not allow channel changes to be combined with SSID, password, or
security changes in one submission.
