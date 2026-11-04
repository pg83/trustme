#![feature(decl_macro)]

pub mod constants { pub const SELECTED: u16 = 137; }
pub use constants::SELECTED as IMPORTED;
pub fn fallback() -> u16 { 0 }
pub trait Marker {}

pub macro imported($value:expr) {
    match $value { IMPORTED => true, _ => false }
}
pub macro function_binding($value:expr) {
    match $value { fallback => 139u16 }
}
pub macro trait_binding($value:expr) {
    match $value { Marker => 149u16 }
}
