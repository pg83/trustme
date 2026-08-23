// Regression: after parsing `.0`, the following `..=` must start a range
// expression instead of being consumed as another postfix dot.

fn take_range(_: core::ops::RangeInclusive<u32>) {}

fn main() {
    let range = (3_u32, 7_u32);
    take_range(range.0..=range.1);
}
