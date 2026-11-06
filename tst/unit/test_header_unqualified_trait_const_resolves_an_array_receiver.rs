trait Width { const N: usize; }
impl Width for u8 { const N: usize = 3; }

trait Tag { const VALUE: usize; }
impl Tag for [u8; 3] { const VALUE: usize = 5; }

struct Buffer([u8; <[u8; <u8 as Width>::N]>::VALUE]);

fn main() {
    let buffer = Buffer([2, 3, 5, 7, 11]);
    assert_eq!(buffer.0[4], 11);
}
