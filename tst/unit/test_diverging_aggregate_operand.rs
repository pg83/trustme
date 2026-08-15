#![allow(dead_code, unreachable_code, unused_must_use)]

struct Pair {
    first: usize,
    second: usize,
}

struct Tuple(usize, usize);

fn return_from_struct_field() -> Pair {
    Pair {
        first: {
            return Pair {
                first: 1,
                second: 2,
            };
        },
        second: 99,
    };
}

fn return_from_tuple_element() -> usize {
    let _: (String, usize) = (String::new(), return 3);
}

fn return_from_tuple_struct_field() -> usize {
    Tuple(0, return 4);
}

fn return_from_array_element() -> usize {
    [0, return 5];
}

fn return_from_call_argument() -> usize {
    core::convert::identity(return 6);
}

fn return_from_borrowed_value() -> usize {
    &(return 7);
}

fn main() {
    let pair = return_from_struct_field();
    assert_eq!((pair.first, pair.second), (1, 2));
    assert_eq!(return_from_tuple_element(), 3);
    assert_eq!(return_from_tuple_struct_field(), 4);
    assert_eq!(return_from_array_element(), 5);
    assert_eq!(return_from_call_argument(), 6);
    assert_eq!(return_from_borrowed_value(), 7);
}
