//@ aux-build: trait_path_hygiene.rs
#![feature(decl_macro)]
use trait_path_hygiene::Compute as Imported;
mod facade { pub use trait_path_hygiene::Compute as Forwarded; }
macro invoke($value:expr) {{
    let method: fn(u16, u8) -> u32 = ::trait_path_hygiene::Compute::<u8>::calc;
    (method($value, 3), ::trait_path_hygiene::Compute::<u8>::calc($value, 7))
}}
fn main() {
    assert_eq!(invoke!(12u16), (142, 146));
    let imported: fn(u16, u8) -> u32 = Imported::<u8>::calc;
    let forwarded: fn(u16, u8) -> u32 = facade::Forwarded::<u8>::calc;
    assert_eq!(imported(4, 2), 61);
    assert_eq!(forwarded(7, 5), 94);
    assert_eq!(<u16 as Imported<u8>>::OFFSET, 19);
}
