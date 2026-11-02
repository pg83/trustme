#[derive(PartialEq, Eq)]
enum Empty {}
const KEY: &[Empty; 0] = &[];
fn classify(value: &[Empty; 0]) -> bool { matches!(value, KEY) }
fn main() { assert!(classify(&[])); }
