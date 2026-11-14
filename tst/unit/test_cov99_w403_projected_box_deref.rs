trait Family {
    type Pointer;
}

impl Family for () {
    type Pointer = Box<u16>;
}

fn take<T: Family<Pointer = Box<u16>>>(value: T::Pointer) -> u16 {
    *value
}

fn main() {
    assert_eq!(take::<()>(Box::new(89)), 89);
}
