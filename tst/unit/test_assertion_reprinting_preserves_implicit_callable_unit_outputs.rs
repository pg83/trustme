//@ compile-variant: -O
macro_rules! check {
    ($body:expr) => { assert!($body) };
}

fn main() {
    check!({
        let value: Option<&dyn Fn()> = None;
        let callback: fn() = || {};
        callback();
        value.is_none()
    });
}
