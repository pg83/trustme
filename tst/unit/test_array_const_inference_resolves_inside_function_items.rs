#![feature(generic_arg_infer)]

fn bytes<const N: usize>(_: [u16; N]) -> usize {
    core::mem::size_of::<[u16; N]>()
}

fn nested<const N: usize>(_: [[u16; N]; 2]) -> usize {
    core::mem::size_of::<[[u16; N]; 2]>()
}

fn main() {
    let inferred = bytes::<_>;
    assert_eq!(inferred([11, 17, 23]), 6);
    let nested_item: fn([[u16; 3]; 2]) -> usize = nested::<_>;
    assert_eq!(nested_item([[1, 2, 3], [4, 5, 6]]), 12);
    let identity = |value: [[u16; _]; 2]| value;
    assert_eq!(identity([[7, 8], [9, 10]])[1][1], 10);
}
