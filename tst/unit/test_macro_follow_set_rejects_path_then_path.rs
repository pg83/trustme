//@ compile-fail: not allowed for
//@ edition: 2021

macro_rules! parse_adjacent {
    ($subject:path $next:path) => { () };
}
fn main() {}
