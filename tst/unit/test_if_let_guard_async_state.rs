// Suspending in an `if let` guard updates the coroutine argument and saved
// local state.  An or-pattern must allow those updates for every cloned guard.
//@ crate-type: lib
#![feature(if_let_guard)]

pub async fn choose(value: u8) {
    match value {
        0 | 1 if let Some(()) = async { Some(()) }.await => (),
        _ => (),
    }
}
