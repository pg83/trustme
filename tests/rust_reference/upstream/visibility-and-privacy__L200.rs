// Extracted from src/visibility-and-privacy.md:200
pub use self::implementation::api;

mod implementation {
    pub mod api {
        pub fn f() {}
    }
}

fn main() {}
