//@ compile-fail: Couldn't find final node
extern crate core as route;

mod route {
    pub struct Unique;
}

use route::Unique;

fn main() {}
