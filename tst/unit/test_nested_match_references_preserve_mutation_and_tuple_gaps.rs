struct Record<T> { value: T, ignored: u8 }
fn main() {
    let mut value = (Record { value: 17u8, ignored: 23 }, 29u8, [3u8, 5, 7]);
    match &mut value {
        (Record { value: selected, .. }, .., [first, _, last]) => {
            *selected += *first + *last;
            *last += 1;
        }
    }
    let (_, .., tail) = &value;
    assert_eq!(*tail, [3, 5, 8]);
    assert_eq!(value.0.value, 27);
    assert_eq!(value.0.ignored, 23);
    let &(first, _, last) = &(17u16, 23, 37);
    assert_eq!((first, last), (17, 37));
}
