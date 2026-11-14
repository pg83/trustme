//@ crate-type: lib
//@ compile-flags: -Z stop-after=typeck

// These Sized vectors collide in the typing-environment hash.
// The snapshot comparison must distinguish the two environments.
pub fn sized_a<
    T0, T1, T2, T3: ?Sized, T4: ?Sized, T5,
    T6: ?Sized, T7, T8, T9: ?Sized, T10, T11,
    T12, T13, T14: ?Sized, T15: ?Sized, T16, T17: ?Sized,
    T18, T19, T20, T21, T22: ?Sized, T23,
    T24: ?Sized, T25: ?Sized, T26, T27, T28: ?Sized, T29: ?Sized,
    T30: ?Sized, T31: ?Sized, T32: ?Sized,
>() {}

pub fn sized_b<
    T0, T1, T2, T3: ?Sized, T4: ?Sized, T5,
    T6: ?Sized, T7, T8, T9: ?Sized, T10, T11,
    T12, T13, T14: ?Sized, T15: ?Sized, T16, T17: ?Sized,
    T18, T19, T20, T21, T22: ?Sized, T23,
    T24: ?Sized, T25: ?Sized, T26, T27, T28: ?Sized, T29,
    T30: ?Sized, T31, T32,
>() {}

