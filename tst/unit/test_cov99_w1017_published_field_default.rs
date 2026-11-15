//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(default_field_values)]

pub struct Telemetry {
    pub sample: u32 = 17,
}
