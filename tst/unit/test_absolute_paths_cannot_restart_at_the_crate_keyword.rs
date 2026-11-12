//@ compile-fail: error:

struct Value;

fn main() {
    let _: ::crate::Value = Value;
}
