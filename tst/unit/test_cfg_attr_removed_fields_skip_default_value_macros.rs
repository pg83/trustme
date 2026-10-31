#![feature(default_field_values)]
macro_rules! excluded_value { () => { compile_error!("removed field default expanded") }; }
struct Record {
    #[cfg_attr(all(), cfg_attr(all(), cfg(any())))]
    removed: u16 = excluded_value!(),
    kept: u16,
}
fn main() { assert_eq!(Record { kept: 59 }.kept, 59); }
