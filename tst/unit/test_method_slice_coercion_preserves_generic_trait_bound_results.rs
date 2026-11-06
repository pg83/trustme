trait Width {
    fn width(&self) -> usize;
}

impl Width for [u8] {
    fn width(&self) -> usize { self.len() }
}

struct Reader;

impl Reader {
    fn width_of<T: Width + ?Sized>(&self, values: &T) -> usize {
        values.width()
    }
}

fn main() {
    let mut bytes = [23u8, 29, 31, 37];
    assert_eq!(Reader.width_of::<[u8]>(&mut bytes), 4);
    assert_eq!(bytes, [23, 29, 31, 37]);
}
