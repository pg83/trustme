//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(auto_traits, more_qualified_paths)]

auto trait Constituent {}

struct Subject;
struct Record {
    value: u8,
}

trait View {
    type Output;
}

impl<T: Constituent> View for T {
    type Output = Record;
}

pub fn check() {
    let <Subject as View>::Output { value } = Record { value: 9 };
    let _: u8 = value;
}
