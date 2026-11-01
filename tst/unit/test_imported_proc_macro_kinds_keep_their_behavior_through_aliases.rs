//@ proc-macro-aux-build: three_imported_macro_kinds.rs
mod imported {
    pub use three_imported_macro_kinds::{make_value as value, replace_function as replace, DeriveMark as Derive};
}

trait Mark { const VALUE: u16; }
#[derive(imported::Derive)]
struct Carrier;

#[imported::replace]
fn attributed() -> u16 { 0 }

fn main() {
    assert_eq!(imported::value!(), 11);
    assert_eq!(attributed(), 17);
    assert_eq!(Carrier::VALUE, 31);
}
