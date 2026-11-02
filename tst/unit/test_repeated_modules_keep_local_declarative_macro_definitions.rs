#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => {
        mod first { $item }
        mod second { $item }
    };
}

copies!(pub mod inner {
    pub macro add($value:expr) { $value + 11u16 }
    pub fn read(value: u16) -> u16 { add!(value) }
});

fn main() {
    assert_eq!(first::inner::read(7), 18);
    assert_eq!(second::inner::read(13), 24);
}
