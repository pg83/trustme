// Extracted from library/core/src/range.rs:7
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::{Range, RangeFrom, RangeInclusive};
    
    let arr = [0, 1, 2, 3, 4];
    assert_eq!(arr[                      ..   ], [0, 1, 2, 3, 4]);
    assert_eq!(arr[                      .. 3 ], [0, 1, 2      ]);
    assert_eq!(arr[                      ..=3 ], [0, 1, 2, 3   ]);
    assert_eq!(arr[     RangeFrom::from(1..  )], [   1, 2, 3, 4]);
    assert_eq!(arr[         Range::from(1..3 )], [   1, 2      ]);
    assert_eq!(arr[RangeInclusive::from(1..=3)], [   1, 2, 3   ]);
}
