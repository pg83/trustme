//@ compile-fail: Unknown macro
pub extern crate std as library;
mod route { pub use library as not_a_macro; }
fn main() { route::not_a_macro!(); }
