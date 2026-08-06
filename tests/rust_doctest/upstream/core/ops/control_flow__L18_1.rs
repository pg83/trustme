// Extracted from library/core/src/ops/control_flow.rs:18
#![allow(unused)]
fn main() {
    use std::ops::ControlFlow;
    
    let r = (2..100).try_for_each(|x| {
        if 403 % x == 0 {
            return ControlFlow::Break(x)
        }
    
        ControlFlow::Continue(())
    });
    assert_eq!(r, ControlFlow::Break(13));
}
