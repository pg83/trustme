//@ aux-build: typed_glob_exports.rs
//@ aux-build: typed_glob_facade.rs
#![feature(trait_alias, extern_types)]
extern crate typed_glob_facade;
use typed_glob_facade::*;
fn display<T: Printable>(value: T) -> String { format!("{:?}", value) }
fn through_second<T: typed_glob_facade::second::Printable>(value: T) -> String {
    use typed_glob_facade::second::*;
    let address: *const Opaque = 0usize as *const source::Foreign;
    assert!(address.is_null());
    assert_eq!(library::mem::size_of::<T>(), 2);
    format!("{:?}", value)
}
fn main() {
    assert_eq!(display(73u32), "73");
    assert_eq!(through_second(79u16), "79");
    assert_eq!(upstream::foundation::mem::size_of::<u64>(), 8);
}
