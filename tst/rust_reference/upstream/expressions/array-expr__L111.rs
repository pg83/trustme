// Extracted from src/expressions/array-expr.md:111
#![allow(unused)]
#![warn(unconditional_panic)]
fn main() {
    // lint is deny by default.
    
    ([1, 2, 3, 4])[2];        // Evaluates to 3
    
    let b = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
    b[1][2];                  // multidimensional array indexing
    
    let x = (["a", "b"])[10]; // warning: index out of bounds
    
    let n = 10;
    let y = (["a", "b"])[n];  // panics
    
    let arr = ["a", "b"];
    arr[10];                  // warning: index out of bounds
}
