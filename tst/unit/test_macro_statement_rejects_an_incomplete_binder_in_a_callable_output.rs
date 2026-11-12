//@ compile-fail: error:

macro_rules! accept_statement {
    ($statement:stmt) => {};
}

fn main() {
    accept_statement!(let _: dyn Fn() -> impl for ;);
}
