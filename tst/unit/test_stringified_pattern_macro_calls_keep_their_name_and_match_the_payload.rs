macro_rules! selected { () => { Some(307u16) }; }
macro_rules! check {
    ($pattern:pat, $value:expr) => {{
        let spelling = stringify!($pattern);
        assert!(matches!($value, $pattern));
        spelling
    }};
}
fn main() {
    let spelling = check!(selected!(), Some(307u16));
    assert!(spelling.contains("selected"));
    assert!(spelling.contains('!'));
}
