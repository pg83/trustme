// Extracted from library/core/src/ops/range.rs:721
#![allow(unused)]
fn main() {
    use std::ops::Bound;
    use Bound::*;
    
    let unbounded_string: Bound<String> = Unbounded;
    
    assert_eq!(unbounded_string.map(|s| s.len()), Unbounded);
}
