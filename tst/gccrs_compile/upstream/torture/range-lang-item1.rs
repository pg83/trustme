// { dg-additional-options "-w" }

pub fn test() {
    let _: std::ops::Range<_> = 1..2;
    let _: std::ops::RangeFrom<_> = 1..;
    let _: std::ops::RangeTo<_> = ..3;
    let _: std::ops::RangeInclusive<_> = 0..=2;
    let _: std::ops::RangeFull = ..;
}
