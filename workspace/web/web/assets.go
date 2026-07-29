package webassets

import _ "embed"

// Index is the embedded telemetry dashboard.
//
//go:embed index.html
var Index []byte
