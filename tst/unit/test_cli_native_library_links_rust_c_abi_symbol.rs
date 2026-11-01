//@ native-static-aux-build: coverage_native_math.rs
//@ compile-flags: -l coverage_native_math
extern "C" { fn coverage_native_scale(value: u32) -> u32; }
fn main() { assert_eq!(unsafe { coverage_native_scale(7) }, 26); }
