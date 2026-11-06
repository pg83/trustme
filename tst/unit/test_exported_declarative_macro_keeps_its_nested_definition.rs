#![feature(decl_macro)]

mod origin {
    #[macro_export]
    pub macro number() {
        211u16
    }

    pub fn value() -> u16 {
        number!()
    }
}

fn main() {
    assert_eq!(origin::number!(), 211);
    assert_eq!(origin::value(), 211);
}
