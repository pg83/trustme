//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// An attribute macro is handed the item as tokens; upstream hands it what was
// written, and pprust adds no parentheses around a referenced or boxed type.
// pin-project's `#[pinned_drop]` checks `self: Pin<&mut Self>` with syn and
// took our `&mut (Self)` for a parenthesised type (futures-concurrency, in
// tokio's dev-dependencies). Parentheses stay where a type of several bounds
// needs them: `&(dyn Debug + Send)`.
use proc_macro_item_passthrough::echo_item_without_added_type_parens;
use std::fmt::Debug;
use std::pin::Pin;

trait PinnedDrop {
    fn drop(self: Pin<&mut Self>);
}

struct J<T, const N: usize>([T; N]);

#[echo_item_without_added_type_parens]
impl<T, const N: usize> PinnedDrop for J<T, N> {
    fn drop(self: Pin<&mut Self>) {}
}

#[echo_item_without_added_type_parens]
fn shapes(a: &u8, b: *const u8, c: &mut Vec<u8>, d: Box<dyn Debug>, e: &(dyn Debug + Send)) -> usize {
    let _ = (b, d, e);
    c.push(*a);
    c.len()
}

fn main() {
    let mut j = J([1u8; 2]);
    Pin::new(&mut j).drop();
    let mut v = Vec::new();
    assert_eq!(shapes(&3, std::ptr::null(), &mut v, Box::new(1), &2u8), 1);
}
