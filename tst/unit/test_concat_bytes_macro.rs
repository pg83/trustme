#![feature(concat_bytes)]

fn main() {
    let bytes: &[u8; 12] = concat_bytes!(
        b'A',
        b"BC",
        [68, b'E', 70],
        [b'G'; 1],
        [72; 2],
        [73u8; 3],
        [65; 0],
    );
    assert_eq!(bytes, b"ABCDEFGHHIII");
    assert_eq!(concat_bytes!(concat_bytes!(b"AB"), b"CD"), b"ABCD");
}
