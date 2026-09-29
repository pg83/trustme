// async-stream's `stream! { .. yield item; .. }` hands its body to a
// procedural macro that looks for the `yield` identifier. The keyword was sent
// spelled `yeild`, the macro found no `yield`, and syn stopped at the
// unknown `yeild item` with "expected `;`".
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

macro_rules! ignore {
    ($($tokens:tt)*) => {};
}

#[echo_item_spelled("yield value")]
fn keywords() -> u8 {
    ignore!(yield value);
    3
}

fn main() {
    assert_eq!(keywords(), 3);
}
