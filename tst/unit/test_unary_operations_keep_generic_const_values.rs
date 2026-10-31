//@ compile-flags: -Coverflow-checks=off
fn invert<const MASK: u8>() -> u8 { let mask = MASK; !mask }
fn negate<const VALUE: i32>() -> i32 { let value = VALUE; -value }
fn toggle<const READY: bool>() -> bool { let ready = READY; !ready }
fn main() {
    assert_eq!(invert::<37>(), 218);
    assert_eq!(invert::<218>(), 37);
    assert_eq!(negate::<37>(), -37);
    assert_eq!(negate::<-37>(), 37);
    assert!(toggle::<false>());
    assert!(!toggle::<true>());
}
