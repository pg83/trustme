// Extracted from library/core/src/ops/control_flow.rs:207
#![allow(unused)]
fn main() {
    use std::ops::ControlFlow;
    
    assert_eq!(ControlFlow::<&str, i32>::Break("Stop right there!").continue_value(), None);
    assert_eq!(ControlFlow::<&str, i32>::Continue(3).continue_value(), Some(3));
}
