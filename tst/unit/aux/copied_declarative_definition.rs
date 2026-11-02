#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => { pub mod first { $item } pub mod second { $item } };
}

copies!(pub mod inner {
    const VALUE: u16 = 59;
    pub macro offset($value:expr) { VALUE + $value }
});
