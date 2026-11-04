#![feature(decl_macro)]

pub fn original(value: u16) -> u16 { value + 2 }
pub macro invoke($value:expr) { original($value) }
