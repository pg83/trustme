#![feature(decl_macro)]

macro define($name:ident) {
    mod generated {
        pub fn fixed() -> u32 { 0 }
        pub fn $name() -> i32 { 0 }
    }

    let _: u32 = generated::fixed();
    let _: i32 = generated::$name();
}

fn main() {
    define!(fixed);
}
