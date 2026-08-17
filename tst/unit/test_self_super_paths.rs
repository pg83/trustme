// `self::super::..` names the parent module just like a leading `super` does.
mod a {
    pub fn foo() -> i32 {
        42
    }

    pub mod b {
        pub mod c {
            pub fn call() -> i32 {
                super::super::foo() + self::super::super::foo()
            }

            pub fn call_use() -> i32 {
                use self::super::super::foo;
                foo()
            }
        }
    }
}

fn main() {
    assert_eq!(a::b::c::call(), 84);
    assert_eq!(a::b::c::call_use(), 42);
}
