// Extracted from src/expressions/operator-expr.md:98
#![allow(unused)]
fn main() {
    // same meanings:
    let a = &&  10;
    let a = & & 10;
    
    // same meanings:
    let a = &&&&  mut 10;
    let a = && && mut 10;
    let a = & & & & mut 10;
}
