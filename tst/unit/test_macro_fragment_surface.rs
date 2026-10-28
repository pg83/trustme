//@ crate-type: lib
#![allow(dead_code)]

macro_rules! expression {
    ($value:expr) => {
        $value + 1
    };
}

macro_rules! statements {
    ($value:stmt) => {
        $value
    };
}

macro_rules! item {
    ($value:item) => {
        $value
    };
}

macro_rules! pattern {
    ($value:pat) => {
        fn pattern(value: i32) -> i32 {
            let $value = value;
            value
        }
    };
}

macro_rules! path {
    ($value:path) => {
        fn path() -> i32 {
            $value as i32
        }
    };
}

macro_rules! block {
    ($value:block) => {
        fn block() -> i32 {
            $value
        }
    };
}

macro_rules! ident {
    ($value:ident) => {
        fn $value() -> i32 {
            1
        }
    };
}

macro_rules! lifetime {
    ($value:lifetime) => {
        fn lifetime<$value>(value: &$value i32) -> &$value i32 {
            value
        }
    };
}

macro_rules! literal {
    ($value:literal) => {
        const LITERAL: i32 = $value;
    };
}

macro_rules! meta {
    ($value:meta) => {
        #[cfg($value)]
        fn meta() {}
    };
}

macro_rules! tree {
    ($($value:tt)*) => {
        $($value)*
    };
}

macro_rules! visibility {
    ($value:vis) => {
        $value fn visibility() {}
    };
}

item! { fn item() {} }
pattern! { x }
path! { std::i32::MAX }
block! { { 3 } }
ident! { identifier }
lifetime! { 'a }
literal! { 42 }
meta! { all() }
tree! { fn tree() {} }
visibility! { pub }
