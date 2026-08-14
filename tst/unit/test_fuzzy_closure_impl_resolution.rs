struct Iter(u8);

impl Iterator for Iter {
    type Item = u8;

    fn next(&mut self) -> Option<u8> {
        if self.0 == 0 {
            None
        } else {
            self.0 -= 1;
            Some(self.0)
        }
    }
}

fn main() {
    assert_eq!(Iter(3).count(), 3);
}
