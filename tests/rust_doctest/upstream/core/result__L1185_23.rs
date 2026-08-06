// Extracted from library/core/src/result.rs:1185
#![allow(unused)]
fn main() {
    let good_year_from_input = "1909";
    let bad_year_from_input = "190blarg";
    let good_year = good_year_from_input.parse().unwrap_or_default();
    let bad_year = bad_year_from_input.parse().unwrap_or_default();
    
    assert_eq!(1909, good_year);
    assert_eq!(0, bad_year);
}
