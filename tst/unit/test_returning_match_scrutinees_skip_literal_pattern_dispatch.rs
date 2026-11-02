//@ compile-variant: -O
fn direct() -> u16 {
    match (return 17) { 23 => 29, _ => 31 }
}
fn both_paths(flag: bool) -> u16 {
    match (if flag { return 37 } else { return 41 }) { 43 => 47, _ => 53 }
}
fn main() {
    assert_eq!(direct(), 17);
    assert_eq!(both_paths(true), 37);
    assert_eq!(both_paths(false), 41);
}
