#![feature(generic_arg_infer)]

fn main() {
    let values: [u16; _] = [3, 5, 7];
    let nested: [[u8; _]; _] = [[1, 2], [3, 4], [5, 6]];
    let empty: [u32; _] = [];
    assert_eq!(values, [3, 5, 7]);
    assert_eq!(nested.len(), 3);
    assert_eq!(nested[2], [5, 6]);
    assert_eq!(empty.len(), 0);
}
