//@ compile-fail: a never pattern is always unreachable
#![feature(never_patterns)]
enum Empty {}
fn impossible(value: Empty) -> u32 { match value { ! } }
fn body(value: Result<u32, Empty>) -> u32 {
    match value { Err(!) => unreachable!(), Ok(value) => value }
}
fn absent(value: Option<Empty>) -> u32 {
    match value { Some(!), None => 467 }
}
fn main() {
    assert_eq!(body(Ok(479)), 479);
    assert_eq!(absent(None), 467);
    let _ = impossible as fn(Empty) -> u32;
}
