#[derive(Eq)]
union Word { number: u32, alternate: u32 }
impl PartialEq for Word {
    fn eq(&self, other: &Self) -> bool { unsafe { self.number == other.number } }
}
fn equal<T: Eq>(left: &T, right: &T) -> bool { left == right }
fn main() {
    assert!(equal(&Word { number: 37 }, &Word { number: 37 }));
    assert!(!equal(&Word { number: 13 }, &Word { number: 37 }));
}
