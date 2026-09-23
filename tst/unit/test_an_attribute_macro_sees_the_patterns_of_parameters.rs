// async-trait's tests take `Tuple(_, _int): Tuple<..>` and `Tuple { 1: _int,
// .. }: Tuple<..>` as parameters. An attribute macro is handed the function
// with its parameter patterns as written: a tuple-struct pattern, a braced
// pattern naming a tuple field by its index and ending in `..`, a reference
// and a slice pattern.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item;

struct Tuple(u8, u16);

#[echo_item]
fn sum(Tuple(_, a): Tuple, Tuple { 1: b, .. }: Tuple, &c: &u16, [d, e]: [u16; 2]) -> u16 {
    a + b + c + d + e
}

fn main() {
    assert_eq!(sum(Tuple(0, 1), Tuple(0, 2), &3, [4, 5]), 15);
}
