fn divide(numerator: f64, denominator: f64) -> Option<f64> {
    if denominator == 0.0 {
        None
    } else {
        Some(numerator / denominator)
    }
}

pub fn test() {
    match divide(2.0, 3.0) {
        Some(value) => println!("Result: {value}"),
        None => println!("Cannot divide by 0"),
    }
}
