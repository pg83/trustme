// A trait body takes inner attributes, which apply to the trait itself. The
// parser rejected the `!` outright.
//
// NOTE: nothing reads them on a trait yet, so they are parsed and dropped.
//
// Same shape as the ui test parser/inner-attr-in-trait-def.rs.
#![deny(non_camel_case_types)]

trait foo_bar {
    #![allow(non_camel_case_types)]

    fn value(&self) -> u8;
}

impl foo_bar for () {
    fn value(&self) -> u8 {
        5
    }
}

fn main() {
    assert_eq!(().value(), 5);
}
