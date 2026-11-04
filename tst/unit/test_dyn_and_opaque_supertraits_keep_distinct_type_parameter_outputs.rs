//@ compile-variant: -O
trait View<T> {
    type Item;
    fn view(&self, input: T) -> Self::Item;
}
trait Both: View<u16, Item = u8> + View<u32, Item = u64> {}
struct Host;
impl View<u16> for Host {
    type Item = u8;
    fn view(&self, input: u16) -> u8 { (input ^ 0x100) as u8 }
}
impl View<u32> for Host {
    type Item = u64;
    fn view(&self, input: u32) -> u64 { u64::from(input) * 65537 }
}
impl Both for Host {}
#[inline(never)]
fn read(value: &dyn Both) -> (u8, u64) {
    (<dyn Both as View<u16>>::view(value, 311), <dyn Both as View<u32>>::view(value, 5))
}
fn hidden() -> impl Both { Host }
fn main() {
    assert_eq!(read(std::hint::black_box(&Host)), (55, 327685));
    let value = hidden();
    assert_eq!(<_ as View<u16>>::view(&value, 315), 59);
    assert_eq!(<_ as View<u32>>::view(&value, 7), 458759);
}
