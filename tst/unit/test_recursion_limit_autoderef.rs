//@ compile-fail: Reached the recursion limit while auto-dereferencing
// `#![recursion_limit]` bounds how far a coercion may follow `Deref`, the same
// way it bounds macro expansion. Two steps are needed to reach `&u8` from
// `&&&u8`, which a limit of one does not allow.

#![recursion_limit = "1"]

fn main() {
    (|_: &u8| {})(&&&1);
}
