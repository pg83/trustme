//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// hyper's client tests build `mpsc::channel::<Result<Frame<Bytes>,
// Box<dyn Error + Send + Sync>>>(0)` inside a `#[tokio::test]` function. The
// item reaches the attribute macro as its pretty-printed text, and the object
// type kept a lifetime bound nobody wrote (the default one, without a name),
// which printed as `+ '` and failed to lex back. Upstream's AST has no bound
// there and prints none.
extern crate proc_macro_item_passthrough;

use proc_macro_item_passthrough::echo_item;
use std::error::Error;

macro_rules! show {
    ($t:ty) => {
        stringify!($t)
    };
}

#[echo_item]
fn boxed() -> usize {
    let errors: Vec<Box<dyn Error + Send + Sync>> = Vec::new();
    let named: Vec<Box<dyn Error + Send + Sync + 'static>> = Vec::new();
    errors.len() + named.len()
}

fn main() {
    assert_eq!(boxed(), 0);
    assert_eq!(show!(Box<dyn Error + Send + Sync>), "Box<dyn Error + Send + Sync>");
    assert_eq!(show!(Box<dyn Error + Send + Sync + 'static>), "Box<dyn Error + Send + Sync + 'static>");
}
