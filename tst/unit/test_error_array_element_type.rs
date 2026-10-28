//@ compile-fail: error
fn main() {
    let _: [i32; 2] = [1, "wrong"];
}
