macro_rules! select {
    ($value:expr; $pattern:pat) => { matches!($value, $pattern) };
}

fn main() {
    assert!(select!('c'; | ..='m'));
    assert!(!select!('x'; | ..='m'));
    assert!(select!('p'; | 'n'..='z' | 'A'..='Z'));
    assert!(!select!('1'; | 'n'..='z' | 'A'..='Z'));
}
