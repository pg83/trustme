#![allow(dead_code)]

struct LaneCount<const N: usize>;

trait SupportedLaneCount {
    type BitMask;
}

macro_rules! supported_lane_count {
    ($($lanes:literal),+) => {
        $(
            impl SupportedLaneCount for LaneCount<$lanes> {
                type BitMask = [u8; ($lanes + 7) / 8];
            }
        )+
    };
}

supported_lane_count!(1, 2, 3, 4);

fn main() {}
