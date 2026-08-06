// Extracted from library/core/src/cmp.rs:616
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    let result = Ordering::Equal.then_with(|| Ordering::Less);
    assert_eq!(result, Ordering::Less);
    
    let result = Ordering::Less.then_with(|| Ordering::Equal);
    assert_eq!(result, Ordering::Less);
    
    let result = Ordering::Less.then_with(|| Ordering::Greater);
    assert_eq!(result, Ordering::Less);
    
    let result = Ordering::Equal.then_with(|| Ordering::Equal);
    assert_eq!(result, Ordering::Equal);
    
    let x: (i64, i64, i64) = (1, 2, 7);
    let y: (i64, i64, i64) = (1, 5, 3);
    let result = x.0.cmp(&y.0).then_with(|| x.1.cmp(&y.1)).then_with(|| x.2.cmp(&y.2));
    
    assert_eq!(result, Ordering::Less);
}
