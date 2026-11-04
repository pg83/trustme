//@ compile-fail: error
struct Seed;
mod outer {
    mod inner {
        use super::super::super::Seed;
    }
}
fn main() {}
