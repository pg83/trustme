#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => {
        mod first { $item pub fn read() -> u16 { value!() } }
        mod second { $item pub fn read() -> u16 { value!() } }
    };
}

copies!(pub macro value() { 37u16 });

fn main() {
    assert_eq!(first::read(), 37);
    assert_eq!(second::read(), 37);
}
