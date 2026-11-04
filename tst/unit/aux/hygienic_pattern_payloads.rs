#![feature(decl_macro)]

pub fn captured() -> u16 { 0 }
pub macro read($value:expr) {
    match $value { captured => captured + 2 }
}
pub fn original(value: u16) -> u16 { value + 3 }
pub macro invoke($value:expr) { original($value) }
