//@ compile-fail: not allowed for
//@ edition: 2021

macro_rules! parse_adjacent {
    ($subject:expr $next:item) => { () };
}
fn main() {}
