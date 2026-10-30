trait Rows: Sized + Copy {
    fn rows(self) -> impl Iterator<Item = impl Iterator<Item = Self> + Clone> + Clone {
        std::iter::once([self, self].into_iter())
    }
    fn repeat<const N: usize>(self) -> impl Iterator<Item = Self> + Clone {
        [self; N].into_iter()
    }
}
impl Rows for u8 {}
impl Rows for u16 {
    fn rows(self) -> impl Iterator<Item = impl Iterator<Item = Self> + Clone> + Clone {
        std::iter::once([self, self, self].into_iter())
    }
}
fn main() {
    let rows = 7u8.rows();
    assert_eq!(rows.clone().next().unwrap().sum::<u8>(), 14);
    assert_eq!(rows.count(), 1);
    let rows = 11u16.rows();
    assert_eq!(rows.clone().next().unwrap().sum::<u16>(), 33);
    assert_eq!(rows.count(), 1);
    let three = 7u8.repeat::<3>();
    assert_eq!(three.clone().sum::<u8>(), 21);
    assert_eq!(three.count(), 3);
    let four = 11u8.repeat::<4>();
    assert_eq!(four.clone().sum::<u8>(), 44);
    assert_eq!(four.count(), 4);
}
