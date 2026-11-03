//@ aux-build: typed_glob_exports.rs
#![feature(trait_alias, extern_types)]
extern crate typed_glob_exports;
use typed_glob_exports::*;
fn render<T: View>(value: T) -> String { format!("{:?}", value) }
fn nested<T: typed_glob_exports::nested::Printable>(value: T) -> String {
    use typed_glob_exports::nested::*;
    let address = 0usize as *const Opaque;
    assert!(address.is_null());
    assert_eq!(library::mem::size_of::<T>(), foundation::mem::size_of::<T>());
    format!("{:?}", value)
}
fn main() {
    assert_eq!(render(61u16), "61");
    assert_eq!(nested([67u8, 71]), "[67, 71]");
    assert!((0usize as *const Foreign).is_null());
}
