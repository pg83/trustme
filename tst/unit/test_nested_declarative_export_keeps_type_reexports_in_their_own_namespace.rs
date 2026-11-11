#![feature(decl_macro)]

pub struct Marker(pub u16);

mod origin {
    #[macro_export]
    pub macro Marker() {
        71u16
    }
}

mod first {
    pub use crate::Marker as Imported;
}

mod second {
    pub use crate::first::Imported as Number;
}

fn main() {
    let number = second::Number(43);
    assert_eq!(number.0, 43);
    assert_eq!(origin::Marker!(), 71);
}
