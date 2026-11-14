trait Width<T, const N: usize = 7> {
    fn width(&self) -> usize;
}

impl Width<u8, 7> for () {
    fn width(&self) -> usize {
        7
    }
}

fn main() {
    let object: &dyn Width<u8> = &();
    assert_eq!(object.width(), 7);
}
