use std::cell::RefCell;

struct Value(u32);

struct Slot<'a> {
    value: &'a mut Value,
}

fn increment(value: &mut Value) {
    value.0 += 1;
}

fn through_ref_mut(slot: &RefCell<Slot<'_>>) {
    let mut guard = slot.borrow_mut();
    increment(guard.value);
    increment(guard.value);
}

fn main() {
    let mut value = Value(0);
    let slot = RefCell::new(Slot { value: &mut value });
    through_ref_mut(&slot);
    assert_eq!(value.0, 2);
}
