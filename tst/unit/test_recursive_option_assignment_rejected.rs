//@ compile-fail: Cannot infer recursive type:

fn main() {
    let mut value = None;
    value = Some(value);
}
