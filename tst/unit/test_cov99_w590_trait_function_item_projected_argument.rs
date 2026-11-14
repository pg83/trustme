trait Element {
    type Item;
}

impl Element for u8 {
    type Item = u16;
}

trait Measure {
    fn item_size<T>() -> usize;
}

impl Measure for () {
    fn item_size<T>() -> usize {
        core::mem::size_of::<T>()
    }
}

fn projected_item_size<T: Element>() -> usize {
    let measure = <() as Measure>::item_size::<T::Item>;
    measure()
}

fn main() {
    assert_eq!(projected_item_size::<u8>(), 2);
}
