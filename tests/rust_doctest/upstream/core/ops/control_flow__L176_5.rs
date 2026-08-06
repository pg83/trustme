// Extracted from library/core/src/ops/control_flow.rs:176
#![allow(unused)]
fn main() {
    use std::ops::ControlFlow;
    
    assert_eq!(ControlFlow::<&str, i32>::Break("Stop right there!").break_value(), Some("Stop right there!"));
    assert_eq!(ControlFlow::<&str, i32>::Continue(3).break_value(), None);
}
