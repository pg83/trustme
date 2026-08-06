// Extracted from library/core/src/ops/range.rs:578
#![allow(unused)]
fn main() {
    let arr = [0, 1, 2, 3, 4];
    assert_eq!(arr[ ..  ], [0, 1, 2, 3, 4]);
    assert_eq!(arr[ .. 3], [0, 1, 2      ]);
    assert_eq!(arr[ ..=3], [0, 1, 2, 3   ]); // This is a `RangeToInclusive`
    assert_eq!(arr[1..  ], [   1, 2, 3, 4]);
    assert_eq!(arr[1.. 3], [   1, 2      ]);
    assert_eq!(arr[1..=3], [   1, 2, 3   ]);
}
