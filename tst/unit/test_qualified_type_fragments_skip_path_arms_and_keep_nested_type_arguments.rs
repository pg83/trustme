trait Route {
    type Item;
}

impl Route for u8 {
    type Item = [u16; 3];
}

trait Finish {
    type Output;
}

impl Finish for [u16; 3] {
    type Output = u32;
}

macro_rules! classify {
    ($path:path) => { 13usize };
    ($ty:ty) => { core::mem::size_of::<$ty>() };
}

fn main() {
    assert_eq!(classify!(<u8 as Route>::Item), 6);
    assert_eq!(classify!(<<u8 as Route>::Item as Finish>::Output), 4);
    assert_eq!(classify!(Option<<u8 as Route>::Item>), 13);
    assert_eq!(classify!(::core::option::Option<<u8 as Route>::Item>), 13);
}
