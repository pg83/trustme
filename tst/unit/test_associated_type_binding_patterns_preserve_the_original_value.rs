trait Family { type Item; }
struct Small;
struct Wide;
impl Family for Small { type Item = u16; }
impl Family for Wide { type Item = (u32, u32); }
#[inline(never)]
fn identity<T: Family>(value: T::Item) -> T::Item { match value { retained => retained } }
fn main() {
    assert_eq!(identity::<Small>(37), 37);
    assert_eq!(identity::<Wide>((41, 43)), (41, 43));
}
