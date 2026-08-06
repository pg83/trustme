// Extracted from src/expressions/if-expr.md:55
#![allow(unused)]
fn main() {
    let x = 3;
    if x == 4 {
        println!("x is four");
    } else if x == 3 {
        println!("x is three");
    } else {
        println!("x is something else");
    }
    
    // `if` can be used as an expression.
    let y = if 12 * 15 > 150 {
        "Bigger"
    } else {
        "Smaller"
    };
    assert_eq!(y, "Bigger");
}
