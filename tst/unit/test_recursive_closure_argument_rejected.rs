//@ compile-fail: Cannot infer recursive type:

fn main() {
    let identity = |value| value;
    let _ = identity(identity);
}
