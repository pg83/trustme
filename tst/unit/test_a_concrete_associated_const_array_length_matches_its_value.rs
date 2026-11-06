trait Width { const SIZE: usize; }
struct Frame;
impl Width for Frame { const SIZE: usize = 3; }

fn preserve(input: [u8; <Frame as Width>::SIZE]) -> [u8; 3] { input }

fn main() {
    assert_eq!(preserve([2, 5, 11]), [2, 5, 11]);
}
