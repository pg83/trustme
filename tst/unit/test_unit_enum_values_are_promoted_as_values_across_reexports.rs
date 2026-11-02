//@ aux-build: enum_value_path_exports.rs
//@ compile-variant: -O
use enum_value_path_exports::{choose, Flag, ACTIVE, CURRENT, SELECTED};
const REFERENCE: &Flag = &ACTIVE;
const FLAGS: [Flag; 3] = [Flag::Off, SELECTED, choose(true)];
fn identify<T: 'static>(_: &T) -> std::any::TypeId { std::any::TypeId::of::<T>() }
fn main() {
    assert_eq!(*REFERENCE, Flag::On);
    assert_eq!(FLAGS, [Flag::Off, ACTIVE, Flag::On]);
    assert_eq!(CURRENT, choose(false));
    assert_eq!(identify(&ACTIVE), std::any::TypeId::of::<Flag>());
    assert_eq!(*std::hint::black_box(&Flag::Off), Flag::Off);
}
