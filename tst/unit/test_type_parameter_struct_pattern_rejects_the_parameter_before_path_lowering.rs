//@ compile-fail: error
fn destructure<T>(value: T) {
    let T { field: _ } = value;
}
fn main() { destructure(()); }
