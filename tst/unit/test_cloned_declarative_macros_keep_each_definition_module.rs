#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => { mod first { $item } mod second { $item } };
}

copies!(pub mod inner {
    const VALUE: u16 = 41;
    pub macro read() { VALUE }
    pub fn value() -> u16 { read!() }
});

fn main() {
    assert_eq!(first::inner::value(), 41);
    assert_eq!(second::inner::value(), 41);
}
