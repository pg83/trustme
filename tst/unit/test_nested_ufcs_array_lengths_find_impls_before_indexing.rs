trait Width { const N: usize; }
impl Width for u8 { const N: usize = 3; }

trait Tag { const VALUE: u8; }
impl Tag for [u8; 3] { const VALUE: u8 = 37; }

const VALUE: u8 = <[u8; <u8 as Width>::N] as Tag>::VALUE;

fn main() {
    assert_eq!(VALUE, 37);
}
