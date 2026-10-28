mod source {
    #[derive(Clone, Debug, PartialEq)]
    pub struct Parcel<T>(pub T);
    impl<T> Parcel<T> {
        pub fn new(value: T) -> Self { Self(value) }
    }
    pub trait Read { type Item; fn read(self) -> Self::Item; }
    impl<T> Read for Parcel<T> {
        type Item = T;
        fn read(self) -> T { self.0 }
    }
    pub const MARK: u8 = 23;
}
mod left { pub use crate::source::*; }
mod right { pub use crate::source::*; }
mod combined {
    pub use crate::left::*;
    pub use crate::right::*;
    pub use crate::left::Parcel as Renamed;
}

fn main() {
    use combined::*;
    type Alias<T> = Renamed<T>;
    let value = Alias::<u16>::new(513);
    assert_eq!(value.clone(), Parcel::new(513));
    assert_eq!(<Alias<u16> as Read>::read(value), 513);
    assert_eq!(MARK, 23);
    let function: fn(u8) -> Alias<u8> = Alias::<u8>::new;
    assert_eq!(function(7).read(), 7);
}
