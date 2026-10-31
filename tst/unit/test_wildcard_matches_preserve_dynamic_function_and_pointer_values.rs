trait Value { fn value(&self) -> u32; }
impl Value for u32 { fn value(&self) -> u32 { *self } }
fn answer() -> u32 { 37 }
fn generic<T>(choice: u8, value: T) -> u8 {
    match (choice, value) { (0 | 1, _) => 17, _ => 23 }
}
fn main() {
    let value = 37u32;
    let object: &dyn Value = &value;
    let pointer = &value as *const u32;
    let function: fn() -> u32 = answer;
    for choice in [0u8, 1, 2] {
        let selected = match (choice, object, pointer, function, answer) {
            (0 | 1, &_, _, _, _) => 17,
            _ => 23,
        };
        assert_eq!(selected, if choice < 2 { 17 } else { 23 });
        assert_eq!(generic(choice, &value), selected);
    }
    assert_eq!((object.value(), function()), (37, 37));
    assert!(std::ptr::eq(pointer, &value));
}
