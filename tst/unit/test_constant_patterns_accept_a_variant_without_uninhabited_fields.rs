#[derive(PartialEq, Eq)]
enum Empty {}
const KEY: Option<Empty> = None;
fn classify(value: Option<Empty>) -> bool { matches!(value, KEY) }
fn main() { assert!(classify(None)); }
