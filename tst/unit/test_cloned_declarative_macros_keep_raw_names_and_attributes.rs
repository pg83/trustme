#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => { mod first { $item } mod second { $item } };
}

copies!(#[allow(unused_macros)] pub macro r#type($value:expr) { $value + 31u16 });

fn main() {
    assert_eq!(first::r#type!(7), 38);
    assert_eq!(second::r#type!(11), 42);
}
