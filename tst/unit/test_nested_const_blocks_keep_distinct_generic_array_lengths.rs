#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn exchange<const N: usize>(
    left: [u8; const { N + 1 }],
    right: [u8; const { N + 2 }],
) -> ([u8; const { N + 2 }], [u8; const { N + 1 }])
where
    [(); const { N + 1 }]:,
    [(); const { N + 2 }]:,
{
    let first: [u8; const { N + 1 }] = left;
    let second: [u8; const { N + 2 }] = right;
    (second, first)
}

fn main() {
    assert_eq!(exchange::<1>([3, 5], [7, 11, 13]), ([7, 11, 13], [3, 5]));
    assert_eq!(exchange::<2>([17, 19, 23], [29, 31, 37, 41]),
               ([29, 31, 37, 41], [17, 19, 23]));
}
