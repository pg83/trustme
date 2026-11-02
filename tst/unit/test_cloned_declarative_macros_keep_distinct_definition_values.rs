#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => {
        mod first { const VALUE: u16 = 61; $item pub fn read() -> u16 { value!() } }
        mod second { const VALUE: u16 = 79; $item pub fn read() -> u16 { value!() } }
    };
}

copies!(pub macro value() { VALUE });

fn main() {
    assert_eq!(first::read(), 61);
    assert_eq!(second::read(), 79);
}
