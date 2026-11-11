macro_rules! unit_type {
    ($name:ident) => { () };
}

macro_rules! check_last_parameter {
    ($($earlier:ident),*) => {
        fn width<$($earlier,)* Target>() -> usize {
            const { core::mem::size_of::<Target>() }
        }

        fn main() {
            assert_eq!(width::<$(unit_type!($earlier),)* u64>(), 8);
            assert_eq!(width::<$(unit_type!($earlier),)* [u16; 3]>(), 6);
        }
    };
}

check_last_parameter!(
    T00, T01, T02, T03, T04, T05, T06, T07,
    T08, T09, T10, T11, T12, T13, T14, T15,
    T16, T17, T18, T19, T20, T21, T22, T23,
    T24, T25, T26, T27, T28, T29, T30, T31,
    T32, T33, T34, T35, T36, T37, T38, T39,
    T40, T41, T42, T43, T44, T45, T46, T47,
    T48, T49, T50, T51, T52, T53, T54, T55,
    T56, T57, T58, T59, T60, T61, T62, T63
);
