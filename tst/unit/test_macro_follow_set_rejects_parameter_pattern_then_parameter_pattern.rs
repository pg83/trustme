//@ compile-fail: not allowed for
//@ edition: 2021

macro_rules! parse_adjacent {
    ($subject:pat_param $next:pat_param) => { () };
}
fn main() {}
