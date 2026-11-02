//@ compile-fail: Type mismatch
use std::ops::Deref;

struct Middle([u16; 2]);
struct Left(Middle);
struct Right(Middle);
impl Deref for Middle {
    type Target = [u16; 2];
    fn deref(&self) -> &Self::Target { &self.0 }
}
impl Deref for Left {
    type Target = Middle;
    fn deref(&self) -> &Middle { &self.0 }
}
impl Deref for Right {
    type Target = Middle;
    fn deref(&self) -> &Middle { &self.0 }
}

fn main() {
    let left = Left(Middle([17, 19]));
    let right = Right(Middle([23, 29]));
    for first in [true, false] {
        let selected = if first { &left } else { &right };
        assert_eq!(selected.0[0], if first { 17 } else { 23 });
    }
}
