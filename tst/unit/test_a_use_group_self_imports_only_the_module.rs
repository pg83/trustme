// actix-cors 0.7's tests write `use actix_web::{test::{self, TestRequest}}`
// and then a plain `#[test]`: `actix_web::test` is both a module and an
// attribute macro, and `self` in a use group imports the module alone
// (rustc's `type_ns_only`), so `#[test]` is still the built-in one.
mod outer {
    pub mod stringify {
        pub fn ok() -> u8 {
            1
        }
    }

    macro_rules! shadowing {
        ($($t:tt)*) => {
            "outer"
        };
    }

    pub(crate) use shadowing as stringify;
}

mod user {
    use crate::outer::stringify::{self};

    pub fn both() -> (&'static str, u8) {
        (stringify!(x), stringify::ok())
    }
}

fn main() {
    assert_eq!(user::both(), ("x", 1));
}
