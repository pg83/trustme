pub const ROOT: u16 = 191;

mod outer {
    pub const PARENT: u16 = 193;
    pub mod inner {
        pub const LOCAL: u16 = 197;
        use self::{super::super::ROOT as GRANDPARENT, super::PARENT as PARENT_ALIAS, LOCAL as LOCAL_ALIAS};

        pub fn read() -> (u16, u16, u16) {
            (GRANDPARENT, PARENT_ALIAS, LOCAL_ALIAS)
        }
    }
}

fn main() {
    assert_eq!(outer::inner::read(), (191, 193, 197));
}
