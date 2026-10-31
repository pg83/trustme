macro_rules! start {
    (paren($range:expr)) => { $range.start };
    (bracket[$range:expr]) => { $range.start };
    (brace{$range:expr}) => { $range.start };
}
macro_rules! sum { ($range:expr; $count:expr) => { $range.take($count).sum::<usize>() }; }
macro_rules! contains { ($range:expr, $value:expr) => { $range.contains(&$value) }; }
fn main() {
    assert_eq!((start!(paren(3..)), start!(bracket[5..]), start!(brace{7..})), (3, 5, 7));
    assert_eq!(sum!(3..; 4), 18);
    assert!(contains!(9.., 13));
}
