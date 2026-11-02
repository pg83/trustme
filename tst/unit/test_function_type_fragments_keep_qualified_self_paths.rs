//@ compile-variant: -O
type Local = u16;
macro_rules! function_type { ($ty:ty) => { type Callback = $ty; }; }
function_type!(fn(self::Local) -> self::Local);
fn increment(value: Local) -> Local { value + 1 }
fn inspect(value: &Local) -> Local { *value + 2 }
fn main() {
    let callback: Callback = increment;
    let borrowed: fn(&self::Local) -> self::Local = inspect;
    assert_eq!(callback(17), 18);
    assert_eq!(borrowed(&23), 25);
}
