//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// paste's tests/test_expr.rs `setter!`: a `macro_rules!` expansion declares
// `let mut new = ..` and passes `new` as an `$obj:expr` fragment into
// `paste! { $obj.[<set_ $field>]($value); }`. rustc hands the proc macro the
// fragment's own tokens, and `new` keeps the context it was written in, where
// the local is visible. We printed the fragment's path from the AST without
// its context, and it named nothing ("Couldn't find variable name 'new'").
use proc_macro_item_passthrough::paste_idents;

struct Counter(i32);

impl Counter {
    fn double(&self) -> i32 {
        self.0 * 2
    }
}

macro_rules! call_double {
    ($obj:expr) => {
        paste_idents! { $obj.[<dou ble>]() }
    };
}

macro_rules! make {
    () => {{
        let new = Counter(2);
        call_double!(new)
    }};
}

fn main() {
    assert_eq!(make!(), 4);
}
