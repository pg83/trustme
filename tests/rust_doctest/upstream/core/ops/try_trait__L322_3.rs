// Extracted from library/core/src/ops/try_trait.rs:322
#![allow(unused)]
#![feature(try_trait_v2)]
fn main() {
    use std::ops::{ControlFlow, FromResidual};
    
    assert_eq!(Result::<String, i64>::from_residual(Err(3_u8)), Err(3));
    assert_eq!(Option::<String>::from_residual(None), None);
    assert_eq!(
        ControlFlow::<_, String>::from_residual(ControlFlow::Break(5)),
        ControlFlow::Break(5),
    );
}
