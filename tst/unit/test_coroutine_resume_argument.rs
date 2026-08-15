#![feature(coroutine_trait)]
#![feature(coroutines)]
#![feature(stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

fn main() {
    let coroutine = #[coroutine] |value: String| {
        yield value;
    };
    let mut coroutine = Box::pin(coroutine);

    match coroutine.as_mut().resume("first".to_string()) {
        CoroutineState::Yielded(value) => assert_eq!(value, "first"),
        CoroutineState::Complete(_) => panic!("coroutine completed early"),
    }
}
