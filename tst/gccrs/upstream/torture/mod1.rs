
mod A {
    pub mod B {
        pub mod C {
            pub struct Foo {
                pub f: i32,
            }
            impl Foo {
                pub fn new() -> Self {
                    Foo { f: 23i32 }
                }
            }
        }
    }
}

fn gccrs_main() -> i32 {
    let a = A::B::C::Foo::new();
    let b = A::B::C::Foo { f: -23i32 };

    a.f + b.f
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
