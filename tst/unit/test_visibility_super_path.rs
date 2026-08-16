// `pub(in self::super::super)` walks up from where the path started, so
// `super` can follow the head of a visibility path. Only a leading `super`
// was accepted.
mod a {
    pub mod b {
        pub(in super::super) struct S;
        pub(in self::super::super) struct T;
        pub(in crate::a) struct U;

        pub fn make() -> (S, T, U) {
            (S, T, U)
        }
    }

    pub fn use_them() {
        let _ = b::make();
    }
}

fn main() {
    a::use_them();
}
