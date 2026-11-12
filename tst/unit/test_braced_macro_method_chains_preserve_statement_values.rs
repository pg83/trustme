#[macro_export]
macro_rules! source {
    () => { Some(439u16) };
}

fn main() {
    let mut count = 0;
    crate::source!{}.map(|value| { count = value; });
    assert_eq!(count, 439);
}
