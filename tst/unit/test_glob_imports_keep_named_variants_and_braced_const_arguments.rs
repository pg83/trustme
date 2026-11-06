//@ aux-build: own_glob_variant_exports.rs
#![allow(dead_code, non_snake_case, non_upper_case_globals)]
extern crate own_glob_variant_exports;
mod local {
    pub enum Local { Named { value: u32 }, Empty }
    pub use Local::*;
}
mod external { pub use own_glob_variant_exports::*; }
mod variants { pub use own_glob_variant_exports::Shape::*; }
mod reexports { pub use own_glob_variant_exports::exported::*; }
mod Count {}
const Count: usize = 3;
fn number<const N: usize>() -> usize { N }
fn read(shape: external::Shape) -> u32 {
    match shape { external::Shape::Named { value } => value, _ => 0 }
}
fn main() {
    assert_eq!(number::<{ Count }>(), 3);
    assert_eq!(external::api_core::mem::size_of::<u16>(), 2);
    let value = local::Named { value: 11 };
    assert!(matches!(value, local::Local::Named { value: 11 }));
    assert_eq!(read(variants::Named { value: 17 }), 17);
    assert_eq!(read(reexports::Named { value: 19 }), 19);
}
