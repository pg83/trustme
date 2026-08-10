// Extracted from src/patterns.md:789
#![allow(unused)]
fn main() {
    struct Struct {
       a: i32,
       b: char,
       c: bool,
    }
    let struct_value = Struct{a: 10, b: 'X', c: false};
    
    let Struct { a, b, c } = struct_value;
}
