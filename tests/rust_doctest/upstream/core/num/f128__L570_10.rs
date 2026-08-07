// Extracted from library/core/src/num/f128.rs:570
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let x = 1.0f128;
    // Clamp value into range [0, 1).
    let clamped = x.clamp(0.0, 1.0f128.next_down());
    assert!(clamped < 1.0);
    assert_eq!(clamped.next_up(), 1.0);
    }
}
