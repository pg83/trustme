use std::cell::Cell;
use std::panic::{catch_unwind, AssertUnwindSafe};
struct Holder<'a> { value: &'a usize }
fn main() {
    let value = 5usize;
    let mut holder = Holder { value: &value };
    let seen = Cell::new(0);
    let result = catch_unwind(AssertUnwindSafe(|| {
        holder.value = &{ seen.set(1); panic!("borrow operand diverges") };
    }));
    assert!(result.is_err());
    assert_eq!(seen.get(), 1);
    assert_eq!(*holder.value, 5);

    let original = &value;
    let mut nested = &original;
    let result = catch_unwind(AssertUnwindSafe(|| {
        nested = &&{ seen.set(2); panic!("nested borrow operand diverges") };
    }));
    assert!(result.is_err());
    assert_eq!(seen.get(), 2);
    assert_eq!(**nested, 5);

    let mut pointer: *const usize = &value;
    let result = catch_unwind(AssertUnwindSafe(|| {
        pointer = { &{ seen.set(3); panic!("raw pointer operand diverges") } };
    }));
    assert!(result.is_err());
    assert_eq!(seen.get(), 3);
    assert_eq!(pointer, &value as *const usize);
}
