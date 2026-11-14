#![feature(decl_macro)]
#![allow(non_upper_case_globals)]

macro make_probe($method:ident, $probe:ident) {
    trait Local {
        const pick: fn() -> usize = Self::$method;
        fn $method() -> usize { 3 }
    }
    impl Local for () {}
    fn $probe() -> usize {
        let callable: fn() -> usize = <() as Local>::pick;
        callable()
    }
}

make_probe!(pick, probe);

fn main() {
    assert_eq!(probe(), 3);
}
