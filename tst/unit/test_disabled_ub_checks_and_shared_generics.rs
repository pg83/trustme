//@ compile-flags: -Zub_checks=no -Zshare_generics=no
#![feature(cfg_ub_checks)]
fn first<T>(pair: (T, T)) -> T { pair.0 }
fn main() {
    assert!(!cfg!(ub_checks));
    assert_eq!(first((String::from("left"), String::from("right"))), "left");
}
