// Extracted from library/core/src/option.rs:23
#![allow(unused)]
fn main() {
    fn divide(numerator: f64, denominator: f64) -> Option<f64> {
        if denominator == 0.0 {
            None
        } else {
            Some(numerator / denominator)
        }
    }

    // The return value of the function is an option
    let result = divide(2.0, 3.0);

    // Pattern match to retrieve the value
    match result {
        // The division was valid
        Some(x) => println!("Result: {x}"),
        // The division was invalid
        None    => println!("Cannot divide by 0"),
    }
}
