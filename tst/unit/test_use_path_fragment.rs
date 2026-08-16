// `use $p;` names the whole path in one macro fragment. The parser demanded a
// `::` after an interpolated path, so only the `use $p::...` forms worked.
//
// Same shape as the upstream test imports/import-prefix-macro.rs.
#![allow(unused_imports)]

mod a {
    pub mod b {
        pub mod c {
            pub struct S;
            pub struct Z;
        }
        pub struct W;
    }
}

macro_rules! import {
    (1 $p:path) => {
        use $p;
    };
    (2 $p:path) => {
        use $p::{Z};
    };
    (3 $p:path) => {
        use $p::*;
    };
    (4 $p:path) => {
        use $p as Renamed;
    };
}

import! { 1 a::b::c::S }
import! { 2 a::b::c }
import! { 3 a::b }
import! { 4 a::b::W }

fn main() {
    let _ = S;
    let _ = Z;
    let _ = c::S;
    let _ = Renamed;
}
