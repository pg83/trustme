macro_rules! select {
    ($value:expr; $pattern:pat) => { matches!($value, $pattern) };
}

fn main() {
    assert!(select!('m'; 'a'..='z'));
    assert!(!select!('A'; 'a'..='z'));
    assert!(select!('c'; ..='m'));
    assert!(!select!('x'; ..='m'));
    assert!(select!('p'; 'n'..='z' | 'A'..='Z'));
}
