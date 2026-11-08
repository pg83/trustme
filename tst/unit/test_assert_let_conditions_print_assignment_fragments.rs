macro_rules! after {
    ($effect:expr, $condition:expr) => {
        assert!(if let () = $effect { $condition } else { false });
    };
}

struct Mark {
    code: u8,
}

fn main() {
    let mut mark = Mark { code: 0 };
    after!(mark = Mark { code: 13 }, mark.code == 13);
    let mut sum = 2u8;
    after!(sum += 5, sum == 7);
}
