## Phazerville

The PHZ operators control the currently active compatible app on a Phazerville
module. Device 1 defaults to address `0x45`; use `PHZ.ADDR` when the module has a
different saved address, such as `70` for `0x46`. `PHZ1:` through `PHZ4:` provide
temporary device selection for one command.

Quantizer writes target the active app's live Q1–Q8 bank. They mark that app's
settings as edited but never save storage. Save later from the Phazerville front
panel if the edit should persist. `PHZ.ERR` reports whether the last PHZ request
was admitted or rejected; admission is not a later application-completion query.

`PHZ.QTZ.CAP` is intentionally not exposed yet. Chord capture needs a correlated
completion result so an empty or unavailable source chord cannot look successful.
