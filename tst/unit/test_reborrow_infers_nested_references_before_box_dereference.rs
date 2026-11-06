use std::mem::{size_of, size_of_val};

fn width<T: ?Sized>(value: &T) -> usize { size_of_val(value) }
fn preserve<T: ?Sized>(value: &T) -> &T { value }

fn main() {
    let boxed = Box::new(41u16);
    let mut nested = &&&boxed;
    let inferred: &_ = &mut nested;
    assert_eq!(width(inferred), size_of::<&&&Box<u16>>());
    assert_eq!(*****preserve(inferred), 41u16);
    let direct: &_ = preserve(&mut nested);
    assert_eq!(width(direct), size_of::<&&&Box<u16>>());
}
