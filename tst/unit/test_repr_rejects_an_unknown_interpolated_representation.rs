//@ compile-fail: Unknown struct repr 'unlisted_representation'
macro_rules! layout {
    ($hint:ty) => { #[repr($hint)] struct Record(u8); };
}
layout!(unlisted_representation);

fn main() {}
