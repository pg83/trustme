#![feature(core_intrinsics, coroutine_trait, coroutines, discriminant_kind, stmt_expr_attributes)]

use std::intrinsics::discriminant_value;
use std::marker::DiscriminantKind;
use std::mem::size_of_val;
use std::ops::{Coroutine, CoroutineState};

fn require_u32<T: DiscriminantKind<Discriminant = u32>>(_: &T) {}

macro_rules! yield_25 {
    () => {
        yield (); yield (); yield (); yield (); yield ();
        yield (); yield (); yield (); yield (); yield ();
        yield (); yield (); yield (); yield (); yield ();
        yield (); yield (); yield (); yield (); yield ();
        yield (); yield (); yield (); yield (); yield ();
    };
}

fn main() {
    let coroutine = #[coroutine] || {
        yield ();
    };

    require_u32(&coroutine);
    let mut coroutine = Box::pin(coroutine);
    assert_eq!(discriminant_value(coroutine.as_mut().get_mut()), 0);
    assert!(matches!(coroutine.as_mut().resume(()), CoroutineState::Yielded(())));
    assert_eq!(discriminant_value(coroutine.as_mut().get_mut()), 3);
    assert!(matches!(coroutine.as_mut().resume(()), CoroutineState::Complete(())));
    assert_eq!(discriminant_value(coroutine.as_mut().get_mut()), 1);

    let large_coroutine = #[coroutine] || {
        yield_25!(); yield_25!(); yield_25!(); yield_25!(); yield_25!();
        yield_25!(); yield_25!(); yield_25!(); yield_25!(); yield_25!();
        yield (); yield (); yield (); yield ();
    };

    assert_eq!(size_of_val(&large_coroutine), 2);
}
