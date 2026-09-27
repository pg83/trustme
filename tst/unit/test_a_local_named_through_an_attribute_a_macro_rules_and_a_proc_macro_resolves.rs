//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// tokio's `#[tokio::test] async fn spawn_wakes_localset() { let local = ...;
// futures::select! { .. local.run_until(..) .. } }`: `select!` is a
// `macro_rules!` that hands its tokens to the `select_internal!` proc macro.
// Every token the attribute gave back reaches that proc macro with the context
// it arrived in, and comes back with it, so `local` still names the binding.
// A token that arrived with no context was sent as if the macro had made it at
// its call site, and came back in the `macro_rules!` expansion's context, where
// the function's `local` is out of sight.
extern crate proc_macro_item_passthrough;
use proc_macro_item_passthrough::{echo, echo_item};

macro_rules! select {
    ($($t:tt)*) => { echo!($($t)*) };
}

#[echo_item]
fn through_all() -> u8 {
    let local = 3u8;
    select!(local + 1)
}

fn main() {
    assert_eq!(through_all(), 4);
}
