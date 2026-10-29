//@ compile-flags: -Zub-checks=yes -Zshare-generics=yes
#![feature(cfg_ub_checks)]
fn duplicate<T: Copy>(value: T) -> (T, T) { (value, value) }
fn main() {
    assert!(cfg!(ub_checks));
    assert_eq!(duplicate(13u16), (13, 13));
    assert_eq!(duplicate(false), (false, false));
}
