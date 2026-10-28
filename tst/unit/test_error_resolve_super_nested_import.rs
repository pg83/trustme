//@ compile-fail: Too many `super` components
mod inner {
    pub fn f() {
        use super::super::Missing;
    }
}
fn main() {}
