//@ compile-fail: still repeating
macro_rules! select {
    ($($value:expr),*) => { $value };
}

fn main() {
    let _ = select!(97u8, 101u8);
}
