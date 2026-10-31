macro_rules! classify {
    ($p:pat, $value:expr, $result:expr) => {
        match $value { $p => $result, _ => 0 }
    };
}
macro_rules! forward {
    ($p:pat, $value:expr) => { classify!($p | 9, $value, 1) };
}
fn main() {
    assert_eq!(classify!(n @ -5..=-1, -3, -n), 3);
    assert_eq!(classify!(ref n @ 2..=5, 4, *n + 1), 5);
    assert_eq!(classify!(ref mut n @ 2..=5, 4, { *n += 2; *n }), 6);
    assert_eq!(forward!(2..=4, 3), 1);
    assert_eq!(forward!(2..=4, 9), 1);
    assert_eq!(forward!(2..=4, 8), 0);
}
