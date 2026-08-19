//@ compile-fail: Only functions with a foreign ABI may be variadic
// Rust's own calling convention has no variadic form, so `fn f(x: i32, ...)`
// names something no call could ever make. Only a foreign ABI has one.

fn takes_rest(_x: i32, ...) {}

fn main() {
    takes_rest(1);
}
