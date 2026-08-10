// Extracted from library/core/src/num/f16.rs:565
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): ABI issues on MSVC
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let x = 1.0f16;
    // Clamp value into range [0, 1).
    let clamped = x.clamp(0.0, 1.0f16.next_down());
    assert!(clamped < 1.0);
    assert_eq!(clamped.next_up(), 1.0);
    }
}
