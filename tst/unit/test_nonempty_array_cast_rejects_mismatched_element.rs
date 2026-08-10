//@ compile-fail: Type mismatch between

struct Value(u8);

fn main() {
    let _ = [0u8] as [Value; 1];
}
