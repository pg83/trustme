fn check<T: Copy>(value: T) {
    let values: &[T] = &[value, value, value];
    let pointers = match values {
        [first, second, third] => {
            [first as *const T, second as *const T, third as *const T]
        }
        _ => unreachable!(),
    };
    for index in 0..values.len() {
        assert_eq!(&values[index] as *const T, pointers[index]);
        assert_eq!(values.iter().nth(index).unwrap() as *const T, pointers[index]);
    }
}

fn main() {
    check(());
    check([0u32; 0]);
}
