//@ compile-fail: Type mismatch
enum Tag { First }
fn identity<T>(value: T) -> T { value }
fn main() {
    let function: fn(_) -> _ = identity;
    let _ = function as Tag;
}
