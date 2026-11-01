//@ aux-build: const_conditional_enum_drop.rs
extern crate const_conditional_enum_drop as source;
const TOTALS: [u16; 8] = [
    source::total(false, false, false), source::total(false, true, false),
    source::total(true, false, false), source::total(true, true, false),
    source::total(false, false, true), source::total(false, true, true),
    source::total(true, false, true), source::total(true, true, true),
];
fn main() { assert_eq!(TOTALS, [20, 20, 20, 20, 0, 0, 0, 0]); }
