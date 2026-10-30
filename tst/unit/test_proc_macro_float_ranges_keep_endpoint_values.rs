//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::echo;
fn main() {
    let inclusive = echo!(0.5..=2.5);
    assert_eq!((*inclusive.start(), *inclusive.end()), (0.5, 2.5));
    assert!(inclusive.contains(&1.25));
    assert!(!inclusive.contains(&2.75));
    let exclusive = echo!(0.125..1.875);
    assert_eq!((exclusive.start, exclusive.end), (0.125, 1.875));
    assert!(!exclusive.contains(&1.875));
    let suffixed = echo!((1.25f32, -2.5f64, 0.75));
    assert_eq!(suffixed.0.to_bits(), 0x3fa00000);
    assert_eq!(suffixed.1.to_bits(), 0xc004000000000000);
    assert_eq!(suffixed.2, 0.75f64);
}
