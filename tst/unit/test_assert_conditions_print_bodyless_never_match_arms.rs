#![feature(never_patterns)]
enum Empty {}
fn main() {
    let value: Option<Empty> = None;
    assert!(match value { Some(!), None => true });
}
