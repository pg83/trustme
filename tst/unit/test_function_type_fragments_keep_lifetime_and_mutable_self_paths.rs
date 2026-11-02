//@ compile-variant: -O
type Local = u16;
macro_rules! function_type { ($ty:ty) => { type Callback = $ty; }; }
function_type!(for<'a> fn(&'a mut self::Local) -> self::Local);
fn increment(value: &mut Local) -> Local { *value += 1; *value }
fn main() {
    let callback: Callback = increment;
    let mut value = 17;
    assert_eq!(callback(&mut value), 18);
    assert_eq!(value, 18);
}
