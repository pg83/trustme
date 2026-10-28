//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::{echo, echo_item};

macro_rules! forward {
    ($item:item) => { echo! { $item } };
}

forward! {
    struct Lending<'a: 'b + 'c, 'b: 'c, 'c> {
        first: &'a u8,
        second: &'b u8,
        third: &'c u8,
    }
}

#[echo_item]
fn shorten<'a: 'b + 'c, 'b: 'c, 'c>(value: &'a u8, _: &'b u8, _: &'c u8) -> &'c u8 {
    value
}

forward! {
    impl<'a: 'b + 'c, 'b: 'c, 'c> Lending<'a, 'b, 'c> {
        fn total(&self) -> u8 { *self.first + *self.second + *self.third }
    }
}

fn main() {
    let first = 5;
    let second = 7;
    let third = 11;
    let loan = Lending { first: &first, second: &second, third: &third };
    assert_eq!(loan.total(), 23);
    assert_eq!(*shorten(&first, &second, &third), 5);
}
