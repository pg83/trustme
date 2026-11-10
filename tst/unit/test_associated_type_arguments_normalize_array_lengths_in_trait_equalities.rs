trait Producer<T> {
    type Product<U>;
}

impl<T> Producer<T> for () {
    type Product<U> = (T, U);
}

fn normalize<P>(
    value: <P as Producer<[u8; { 1 + 2 }]>>::Product<[u16; { 7 - 2 }]>,
) -> ([u8; 3], [u16; 5])
where
    P: Producer<
        [u8; { 6 / 2 }],
        Product<[u16; { 2 + 3 }]> = ([u8; 3], [u16; 5]),
    >,
{
    value
}

fn main() {
    let pair = normalize::<()>(([3, 5, 8], [13, 21, 34, 55, 89]));
    assert_eq!(pair.0, [3, 5, 8]);
    assert_eq!(pair.1, [13, 21, 34, 55, 89]);
}
