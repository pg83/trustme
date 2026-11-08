//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs fn sum_twice
macro_rules! step { ($value:expr) => { $value + 1 }; }
macro_rules! twice { ($body:block) => {{ $body; $body; }}; }
fn sum_twice() -> u16 {
    let mut total = 0;
    twice!({
        for value in [2u8, 4, 6] {
            total += step!(value) as u16;
        }
    });
    total
}
fn main() { assert_eq!(sum_twice(), 30); }
