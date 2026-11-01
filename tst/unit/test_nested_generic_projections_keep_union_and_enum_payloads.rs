//@ compile-variant: -O
use std::fmt::Debug;

#[derive(Clone, Copy)]
#[repr(C)]
struct Pair<T: Copy> { first: T, second: T }
union Storage<T: Copy> { array: [T; 2], pair: Pair<T> }
enum Selection<T: Copy> { One(T), Both(Pair<T>) }

fn check<T: Copy + Eq + Debug>(first: T, second: T) {
    let mut storage = Box::new(Storage { array: [first, second] });
    unsafe {
        let pair = &mut storage.pair;
        assert_eq!(pair.first, first);
        pair.second = second;
        let Storage { array: values } = *storage;
        let index = std::hint::black_box(1usize);
        assert_eq!(values[index], second);
        let slice: &[T] = &values;
        assert_eq!(slice[index], second);
        let pointer = slice.as_ptr();
        assert_eq!(*pointer, first);
        assert_eq!(*pointer.add(index), second);
    }
    for choice in [Selection::One(first), Selection::Both(Pair { first, second })] {
        let value = match choice {
            Selection::One(value) => value,
            Selection::Both(Pair { first: value, second: other }) => {
                assert_eq!(other, second);
                value
            }
        };
        assert_eq!(value, first);
    }
}

fn main() {
    check(17u128, 1u128 << 100);
    check((3u16, true), (11u16, false));
}
