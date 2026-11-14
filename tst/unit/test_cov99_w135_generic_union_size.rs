//@ compile-flags: -O

union Payload<T: Copy> {
    value: T,
}

fn union_size<T: Copy>() -> usize {
    std::mem::size_of::<Payload<T>>()
}

fn main() {
    assert_eq!(union_size::<u32>(), 4);
}
