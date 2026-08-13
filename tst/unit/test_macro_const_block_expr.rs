//@ edition: 2024

macro_rules! accept_expr {
    ($expression:expr) => {};
}

fn main() {
    accept_expr!(const { 1 });
}
