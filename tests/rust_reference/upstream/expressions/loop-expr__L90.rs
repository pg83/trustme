// Extracted from src/expressions/loop-expr.md:90
#![allow(unused)]
fn main() {
    let mut x = vec![1, 2, 3];
    
    while let Some(y) = x.pop() {
        println!("y = {}", y);
    }
    
    while let _ = 5 {
        println!("Irrefutable patterns are always true");
        break;
    }
}
