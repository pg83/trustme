trait Marker {}
impl Marker for u32 {}
struct Package<T>(T);
impl<T: Marker> Package<T> {
    const EMPTY: Option<T> = None;
}

fn main() {
    let empty: Option<u32> = Package::<_>::EMPTY;
    assert!(empty.is_none());
}
