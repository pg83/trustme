#![feature(type_ascription)]

const LEN: usize = type_ascribe!(3, usize);

fn main() {
    let array = [1, 2, 3];
    let slice: &[i32] = type_ascribe!(&array, &[i32; LEN]);
    assert_eq!(slice, [1, 2, 3]);

    let mut values = Vec::new();
    type_ascribe!(values, Vec<u8>) = vec![4, 5, 6];
    assert_eq!(values, [4, 5, 6]);
}
