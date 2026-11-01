//@ aux-build: reexported_payload_use_targets.rs
//@ compile-fail: Unknown enum variant
mod facade { pub use reexported_payload_use_targets::Payload; }
use crate::facade::Payload::Missing;
fn main() {}
