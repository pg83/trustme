// Extracted from src/expressions/operator-expr.md:82
#![allow(unused)]
fn main() {
    {
        // a temporary with value 7 is created that lasts for this scope.
        let shared_reference = &7;
    }
    let mut array = [-2, 3, 9];
    {
        // Mutably borrows `array` for this scope.
        // `array` may only be used through `mutable_reference`.
        let mutable_reference = &mut array;
    }
}
