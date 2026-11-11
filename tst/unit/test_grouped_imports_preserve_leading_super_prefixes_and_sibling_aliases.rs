pub const ROOT: u16 = 179;

mod outer {
    pub const PARENT: u16 = 181;
    pub mod inner {
        use super::{super::ROOT as GRANDPARENT, PARENT as PARENT_ALIAS};

        pub fn read() -> (u16, u16) {
            (GRANDPARENT, PARENT_ALIAS)
        }
    }
}

fn main() {
    assert_eq!(outer::inner::read(), (179, 181));
}
