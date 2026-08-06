// Extracted from library/core/src/iter/traits/iterator.rs:2396
#![allow(unused)]
fn main() {
    use std::ops::ControlFlow;
    
    let triangular = (1..30).try_fold(0_i8, |prev, x| {
        if let Some(next) = prev.checked_add(x) {
            ControlFlow::Continue(next)
        } else {
            ControlFlow::Break(prev)
        }
    });
    assert_eq!(triangular, ControlFlow::Break(120));
    
    let triangular = (1..30).try_fold(0_u64, |prev, x| {
        if let Some(next) = prev.checked_add(x) {
            ControlFlow::Continue(next)
        } else {
            ControlFlow::Break(prev)
        }
    });
    assert_eq!(triangular, ControlFlow::Continue(435));
}
