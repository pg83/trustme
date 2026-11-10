//@ aux-build: module_lookup_macro_exports.rs

extern crate module_lookup_macro_exports as imported;
use imported::exports::adjusted;

static BASE: u16 = 17;

mod values {
    pub static OFFSET: u16 = 31;
    pub trait Read {
        fn read(&self) -> u16;
    }
    pub struct Number(pub u16);
    impl Read for Number {
        fn read(&self) -> u16 { self.0 + OFFSET }
    }
    impl Number {
        pub const fn new(value: u16) -> Self { Self(value) }
    }
    pub enum Width { Small = 3, Large = 11 }
}

fn local(value: u16) -> u16 {
    use values::Read;
    struct Local(values::Number);
    impl Read for Local {
        fn read(&self) -> u16 { self.0.read() + 7 }
    }
    Local(values::Number::new(value)).read()
}

fn main() {
    assert_eq!(local(BASE), 55);
    assert_eq!(adjusted!(local(23)), 90);
    assert_eq!(values::Width::Small as u16 + values::Width::Large as u16, 14);
    assert_eq!(values::OFFSET, 31);
}
