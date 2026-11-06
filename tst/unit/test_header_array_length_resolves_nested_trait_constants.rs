trait Width { const N: usize; }
impl Width for u8 { const N: usize = 3; }

trait Tag { const VALUE: usize; }
impl Tag for [u8; 3] { const VALUE: usize = 5; }

struct Buffer([u8; <[u8; <u8 as Width>::N] as Tag>::VALUE]);

fn main() {
    let buffer = Buffer([7, 11, 13, 17, 19]);
    assert_eq!(buffer.0[4], 19);
}
