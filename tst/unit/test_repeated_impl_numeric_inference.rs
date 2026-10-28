struct Pair<T, U>(T, U);

trait Combine { type Output; fn combine(self) -> Self::Output; }
impl<T: Copy + std::ops::Add<Output = T>> Combine for Pair<T, T> {
    type Output = T;
    fn combine(self) -> T { self.0 + self.1 }
}

fn main() {
    let a: u8 = Pair(3u8, 4).combine();
    let b: i16 = Pair(-3, 4i16).combine();
    let c: f32 = Pair(1.25f32, 2.5).combine();
    let d: f64 = Pair(1.25, 2.5f64).combine();
    assert_eq!((a, b, c, d), (7, 1, 3.75, 3.75));
}
