// Extracted from library/core/src/ops/control_flow.rs:239
#![allow(unused)]
#![feature(control_flow_into_value)]
fn main() {
    use std::ops::ControlFlow;

    assert_eq!(ControlFlow::<i32, i32>::Break(1024).into_value(), 1024);
    assert_eq!(ControlFlow::<i32, i32>::Continue(512).into_value(), 512);
}
