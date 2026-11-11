fn route() -> u16 {
    11
}

mod route {
    pub fn step() -> u16 {
        17
    }

    pub mod step {
        pub fn leaf() -> u16 {
            23
        }
    }
}

fn main() {
    let calls: [fn() -> u16; 3] = [route, route::step, route::step::leaf];
    let results = calls.map(|call| std::hint::black_box(call)());
    assert_eq!(results, [11, 17, 23]);
}
