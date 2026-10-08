// keccak's shape: an associated constant that borrows a block's value, used
// only from a generic function.
const RC: [u64; 4] = [1, 2, 3, 4];

pub trait LaneSize: Copy + Into<u64> + 'static {
    const RC: &[Self];
}

impl LaneSize for u8 {
    const RC: &[Self] = &{
        let mut res = [0; 3];
        let mut i = 0;
        while i < res.len() {
            res[i] = RC[i] as Self;
            i += 1;
        }
        res
    };
}

pub fn sum<L: LaneSize>() -> u64 {
    let mut total = 0;
    for &lane in L::RC {
        total += lane.into();
    }
    total
}
