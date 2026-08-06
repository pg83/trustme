// Extracted from src/statements.md:125
#![allow(unused)]
fn main() {
    // bad: the block's type is i32, not ()
    // Error: expected `()` because of default return type
    // if true {
    //   1
    // }
    
    // good: the block's type is i32
    if true {
      1
    } else {
      2
    };
}
