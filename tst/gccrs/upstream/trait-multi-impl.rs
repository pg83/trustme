#![feature(lang_items)]

trait TraitA {
    fn do_a(&self) -> i32;
}

mod mod_a {
    use super::TraitA;

    pub struct StructX {
        pub val: i32,
    }

    impl TraitA for StructX {
        fn do_a(&self) -> i32 {
            self.val * 10
        }
    }
}

mod mod_b {
    use super::TraitA;

    pub struct StructX {
        pub val: i32,
    }

    impl TraitA for StructX {
        fn do_a(&self) -> i32 {
            self.val * 20
        }
    }
}

fn gccrs_main() -> i32 {
    let a = mod_a::StructX { val: 10 };
    let b = mod_b::StructX { val: 20 };

    let dyn_a = &a as &dyn TraitA;
    let dyn_b = &b as &dyn TraitA;

    if dyn_a.do_a() == 100 && dyn_b.do_a() == 400 {
        0
    } else {
        1
    }
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
