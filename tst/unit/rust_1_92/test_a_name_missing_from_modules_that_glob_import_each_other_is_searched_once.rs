// Twelve modules, each glob-importing all the others, each with
// `use std::fmt::Debug;`. Expansion asks whether that `use` names a macro,
// which looks `std` up as an item of the module, through its glob imports,
// before the extern prelude. rustc's resolution of a name in a module is one
// fact of that module: glob imports copy bindings into the importer's table
// and a lookup reads it. Our lookup walked the import graph and searched a
// module again on every path that reached it, so a name found nowhere took
// time in the number of simple paths, n!: rav1e, under image, never left
// macro expansion.
mod m0 {
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f0() -> usize {
        0
    }
}
mod m1 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f1() -> usize {
        1
    }
}
mod m2 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f2() -> usize {
        2
    }
}
mod m3 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f3() -> usize {
        3
    }
}
mod m4 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f4() -> usize {
        4
    }
}
mod m5 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f5() -> usize {
        5
    }
}
mod m6 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f6() -> usize {
        6
    }
}
mod m7 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f7() -> usize {
        7
    }
}
mod m8 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f8() -> usize {
        8
    }
}
mod m9 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f9() -> usize {
        9
    }
}
mod m10 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m11::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f10() -> usize {
        10
    }
}
mod m11 {
    #[allow(unused_imports)]
    use crate::m0::*;
    #[allow(unused_imports)]
    use crate::m1::*;
    #[allow(unused_imports)]
    use crate::m2::*;
    #[allow(unused_imports)]
    use crate::m3::*;
    #[allow(unused_imports)]
    use crate::m4::*;
    #[allow(unused_imports)]
    use crate::m5::*;
    #[allow(unused_imports)]
    use crate::m6::*;
    #[allow(unused_imports)]
    use crate::m7::*;
    #[allow(unused_imports)]
    use crate::m8::*;
    #[allow(unused_imports)]
    use crate::m9::*;
    #[allow(unused_imports)]
    use crate::m10::*;
    #[allow(unused_imports)]
    use std::fmt::Debug;
    pub fn f11() -> usize {
        11
    }
}
fn main() {
    let total = m0::f0() + m1::f1() + m2::f2() + m3::f3() + m4::f4() + m5::f5() + m6::f6() + m7::f7() + m8::f8() + m9::f9() + m10::f10() + m11::f11();
    assert_eq!(total, 66);
}
