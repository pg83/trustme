// Extracted from library/core/src/ops/control_flow.rs:159
#![allow(unused)]
fn main() {
    use std::ops::ControlFlow;
    
    assert!(!ControlFlow::<&str, i32>::Break("Stop right there!").is_continue());
    assert!(ControlFlow::<&str, i32>::Continue(3).is_continue());
}
