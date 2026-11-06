//@ reject-flags: --edition=2021 --cfg shared => associated static items are not allowed || error
//@ reject-flags: --edition=2021 --cfg mutable => associated static items are not allowed || error
macro_rules! members {
    ($item:item) => { trait Family { $item const FALLBACK: u8 = 29; } };
}
#[cfg(shared)]
members!(static VALUE: u8 = 23;);
#[cfg(mutable)]
members!(static mut VALUE: u8 = 23;);
#[cfg(not(any(shared, mutable)))]
members!(const VALUE: u8;);
#[cfg(not(any(shared, mutable)))]
impl Family for () { const VALUE: u8 = 23; }
fn main() {
    #[cfg(not(any(shared, mutable)))] {
        assert_eq!(<() as Family>::VALUE, 23);
        assert_eq!(<() as Family>::FALLBACK, 29);
    }
}
