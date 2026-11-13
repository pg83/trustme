//@ compile-fail: error:

macro_rules! undelimited_matcher {
    token => {}
}

fn main() {}
