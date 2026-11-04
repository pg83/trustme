mod tree {
    pub mod leaf {
        pub struct Seed(pub u16);
        pub const COUNT: u16 = 701;
        macro_rules! height { () => { 709u16 }; }
        pub(crate) use height;
    }
}
use tree as grove;
use grove as orchard;
mod view {
    pub(crate) use crate::orchard::leaf as branch;
    pub(crate) use self::branch::{Seed, COUNT, height};
}
fn main() {
    let seed = view::Seed(view::COUNT);
    assert_eq!(seed.0 + view::height!(), 1410);
}
