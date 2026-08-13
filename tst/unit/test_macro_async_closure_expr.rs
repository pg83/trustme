macro_rules! accept_expr {
    ($expression:expr) => {};
}

fn main() {
    accept_expr!(async || {});
}
