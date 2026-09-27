//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// paste's tests/test_expr.rs puts `macro_rules! setter { ... }` inside a
// module under `#[rustversion::since(1.46)]`. rustc hands an attribute macro
// the item's tokens as written; we rebuilt a macro invocation item from the
// AST as `path ! ( .. ) ;`, dropping the `setter` name and the braces, and
// the module came back with "macro_rules! requires an identifier". rustc
// keeps a macro call's delimiter with its tokens (`DelimArgs`).
use proc_macro_item_passthrough::echo_item;

#[echo_item]
mod local {
    macro_rules! setter {
        ($field:ident, $value:expr) => {{
            let $field = $value;
            $field
        }};
    }

    pub fn run() -> i32 {
        setter!(val, 42)
    }
}


#[echo_item]
mod bracketed {
    macro_rules! double {
        ($e:expr) => {
            $e * 2
        };
    }
    pub const SIX: i32 = double![3];
}

fn main() {
    assert_eq!(local::run(), 42);
    assert_eq!(bracketed::SIX, 6);
}
