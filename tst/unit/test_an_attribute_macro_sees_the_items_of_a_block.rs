// async-trait's tests declare `const MAX: u16 = 128;` inside an async fn
// body. Upstream's items are statements of their block and reach an
// attribute macro where they are written; our printer of a body lost the
// items of every block and the body then named an undefined `MAX`.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

#[echo_item_spelled("MAX")]
fn body_items() -> u16 {
    const MAX: u16 = 128;
    struct Local(u8);
    fn helper() -> u16 {
        2
    }
    let nested = {
        const INNER: u16 = 5;
        INNER
    };
    MAX + Local(1).0 as u16 + helper() + nested
}

fn main() {
    assert_eq!(body_items(), 136);
}
