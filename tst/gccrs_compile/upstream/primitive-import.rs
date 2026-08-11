
mod primitive {
    pub use std::primitive::i32;
}

pub fn foo() -> primitive::i32 {
    1
}
