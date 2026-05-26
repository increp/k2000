# Architecture

Deep dives into specific architectural pieces. Where a spec answers *what* we're building, an architecture doc answers *how a subsystem works* in enough detail to read or modify it without surprises.

A subsystem earns its own architecture doc when it's load-bearing and non-trivial — something that more than one other module depends on and that wouldn't be obvious from reading the code alone.

## Index

| Doc | What it covers |
|---|---|
| [DSPBlock interface](dsp-block-interface.md) | The polymorphic abstraction every VAST processing block conforms to — the core of the VAST architecture. |
