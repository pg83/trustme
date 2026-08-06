// Extracted from library/core/src/iter/traits/iterator.rs:1526
#![allow(unused)]
fn main() {
    let options = vec![Some(123), Some(321), None, Some(231)];
    let flattened_options: Vec<_> = options.into_iter().flatten().collect();
    assert_eq!(flattened_options, [123, 321, 231]);
    
    let results = vec![Ok(123), Ok(321), Err(456), Ok(231)];
    let flattened_results: Vec<_> = results.into_iter().flatten().collect();
    assert_eq!(flattened_results, [123, 321, 231]);
}
