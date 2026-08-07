// Extracted from library/core/src/ops/try_trait.rs:202
#![allow(unused)]
#![feature(try_trait_v2)]
fn main() {
    use std::ops::{ControlFlow, Try};

    assert_eq!(Ok::<_, String>(3).branch(), ControlFlow::Continue(3));
    assert_eq!(Err::<String, _>(3).branch(), ControlFlow::Break(Err(3)));

    assert_eq!(Some(3).branch(), ControlFlow::Continue(3));
    assert_eq!(None::<String>.branch(), ControlFlow::Break(None));

    assert_eq!(ControlFlow::<String, _>::Continue(3).branch(), ControlFlow::Continue(3));
    assert_eq!(
        ControlFlow::<_, String>::Break(3).branch(),
        ControlFlow::Break(ControlFlow::Break(3)),
    );
}
