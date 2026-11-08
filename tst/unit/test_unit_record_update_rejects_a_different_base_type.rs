//@ compile-fail: Type mismatch
struct Unit;

fn main() {
    let _value = Unit { ..7u8 };
}
