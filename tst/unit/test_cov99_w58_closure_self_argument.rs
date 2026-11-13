//@ compile-fail: Cannot infer recursive type:
//@ compile-flags: -Cpanic=abort

fn main() {
    let identity = |value| value;
    identity(identity);
}
